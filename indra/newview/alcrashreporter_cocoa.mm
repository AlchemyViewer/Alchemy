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
#include "llsdserialize.h"
#include "llstartup.h"
#include "llsys.h"
#include "lluuid.h"
#include "llversioninfo.h"

#include <fstream>
#include <iterator>

namespace
{
    bool sEngaged = false;

    // What the previous run left in the static debug file, read at init
    // before this run rewrites it; the reporter may only engage later.
    ALCrashReporter::PreviousRun sLastRun;
    std::string sLastRunDebugInfo;
    bool sLastRunRead = false;

    NSString* ns(const std::string& text)
    {
        NSString* string = [NSString stringWithUTF8String:text.c_str()];
        return string ? string : @"";
    }

    void read_last_run()
    {
        if (sLastRunRead)
        {
            return;
        }
        sLastRunRead = true;

        const std::string path = *LLAppViewer::instance()->getStaticDebugFile();
        llifstream file(path);
        if (!file.is_open())
        {
            return;
        }
        sLastRunDebugInfo.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

        std::istringstream contents(sLastRunDebugInfo);
        LLSD info;
        if (LLSDSerialize::deserialize(info, contents, LLSDSerialize::SIZE_UNLIMITED))
        {
            sLastRun = ALCrashReporter::previousRun(info);
        }
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

        SentryObjCEvent* event = [[SentryObjCEvent alloc] initWithLevel:SentryObjCLevelInfo];
        event.message = [[SentryObjCMessage alloc] initWithFormatted:@"Files from the run that crashed"];
        event.logger = @"crash-context";

        NSMutableDictionary<NSString*, NSString*>* tags = [NSMutableDictionary dictionary];
        if (!sLastRun.runId.empty())
        {
            tags[@"crashed_run_id"] = ns(sLastRun.runId);
        }
        if (!sLastRun.region.empty())
        {
            tags[@"crashed_region"] = ns(sLastRun.region);
        }
        event.tags = tags;
        if (!sLastRun.fatalMessage.empty())
        {
            event.extra = @{ @"fatal_message" : ns(sLastRun.fatalMessage) };
        }

        SentryObjCScope* scope = [[SentryObjCScope alloc] init];
        for (const std::string& path : { sLastRun.logFile, sLastRun.userSettingsFile, sLastRun.accountSettingsFile })
        {
            if (!path.empty() && LLFile::isfile(path))
            {
                [scope addAttachment:[[SentryObjCAttachment alloc] initWithPath:ns(path)]];
            }
        }
        if (!sLastRunDebugInfo.empty())
        {
            NSData* data = [NSData dataWithBytes:sLastRunDebugInfo.data() length:sLastRunDebugInfo.size()];
            [scope addAttachment:[[SentryObjCAttachment alloc] initWithData:data filename:@"static_debug_info.log"]];
        }

        [SentryObjCSDK captureEvent:event withScope:scope];
    }

    bool engage()
    {
        LLAppViewer* app = LLAppViewer::instance();
        const LLVersionInfo& version = LLVersionInfo::instance();

        const std::string release = ALCrashReporter::releaseName(version.getMajor(), version.getMinor(),
                                                                 version.getPatch(), version.getBuild());
        const std::string environment = version.getChannel();
        const std::string dist = std::to_string(version.getBuild());
        const std::string cache = gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "sentry");

        [SentryObjCSDK startWithConfigureOptions:^(SentryObjCOptions* options) {
            options.dsn = @AL_SENTRY_DSN;
            options.releaseName = ns(release);
            options.environment = ns(environment);
            options.dist = ns(dist);
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
            options.attachScreenshot = NO;
            options.attachViewHierarchy = NO;
        }];

        if (![SentryObjCSDK isEnabled])
        {
            LL_WARNS("CrashReporter") << "Sentry did not start; crashes go unreported" << LL_ENDL;
            return false;
        }
        sEngaged = true;

        const std::string run_id = ALCrashReporter::runId();
        const std::string os = LLOSInfo::instance().getOSStringSimple();
        const std::string app_state = LLStartUp::getStartupStateString();
        const bool second_instance = app->isSecondInstance();
        [SentryObjCSDK configureScope:^(SentryObjCScope* scope) {
            [scope setTagValue:ns(run_id) forKey:@"run_id"];
            [scope setTagValue:ns(os) forKey:@"os"];
            [scope setTagValue:(second_instance ? @"true" : @"false") forKey:@"second_instance"];
            [scope setTagValue:ns(app_state) forKey:@"app_state"];
        }];

        if ([SentryObjCSDK lastRunStatus] == SentryObjCLastRunStatusDidCrash)
        {
            report_last_run();
        }

        LL_INFOS("CrashReporter") << "Sentry engaged for " << version.getChannelAndVersion() << LL_ENDL;
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
    read_last_run();
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
    SentryObjCEvent* event = [[SentryObjCEvent alloc] initWithLevel:SentryObjCLevelFatal];
    event.message = [[SentryObjCMessage alloc] initWithFormatted:ns(description)];
    event.logger = @"watchdog";
    event.tags = @{
        @"watchdog_state" : ns(LLAppViewer::instance()->getMainloopWatchdogState()),
        @"app_state" : ns(LLStartUp::getStartupStateString()),
    };
    [SentryObjCSDK captureEvent:event];
    return true;
}

bool ALCrashReporter::handleException(void*)
{
    return false;
}
