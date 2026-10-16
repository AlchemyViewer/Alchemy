/**
 * @file alcrashreporter_run.cpp
 * @brief This run as its reports describe it, and how the run before it ended
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

#include "llappviewer.h"
#include "llfile.h"
#include "llmutex.h"
#include "llsdserialize.h"
#include "llstartup.h"
#include "llsys.h"
#include "llversioninfo.h"

#include <sstream>

namespace
{
    // Written once by readLastRun(), before any reporter starts, and only
    // read after.
    ALCrashReporter::PreviousRun sLastRun;
    std::string sLastRunDebugInfo;

    // The report the previous run ended with: a freeze's from the record it
    // left, a crash's from the reporter, which may find it off the main
    // thread.
    LLMutex sPreviousReportMutex;
    std::optional<ALCrashReporter::PreviousReport> sPreviousReport;
}

ALCrashReporter::Release ALCrashReporter::release()
{
    const LLVersionInfo& version = LLVersionInfo::instance();
    Release release;
    release.name = releaseName(version.getMajor(), version.getMinor(), version.getPatch(), version.getBuild());
    release.environment = version.getChannel();
    release.dist = std::to_string(version.getBuild());
    return release;
}

std::vector<std::pair<std::string, std::string>> ALCrashReporter::commonTags()
{
    return {
        { "run_id", runId() },
        { "os", LLOSInfo::instance().getOSStringSimple() },
        { "second_instance", LLAppViewer::instance()->isSecondInstance() ? "true" : "false" },
        { "app_state", LLStartUp::getStartupStateString() },
    };
}

void ALCrashReporter::readLastRun()
{
    LLAppViewer* app = LLAppViewer::instance();
    if (app->isSecondInstance())
    {
        return;
    }

    sLastRunDebugInfo = LLFile::getContents(*app->getStaticDebugFile());
    if (!sLastRunDebugInfo.empty())
    {
        std::istringstream contents(sLastRunDebugInfo);
        LLSD info;
        if (LLSDSerialize::deserialize(info, contents, LLSDSerialize::SIZE_UNLIMITED))
        {
            sLastRun = previousRun(info);
        }
    }

    std::optional<PreviousReport> report = takeRecordedReport(reportRecordFile());
    LLMutexLock lock(&sPreviousReportMutex);
    sPreviousReport = std::move(report);
}

const ALCrashReporter::PreviousRun& ALCrashReporter::lastRun()
{
    return sLastRun;
}

const std::string& ALCrashReporter::lastRunDebugInfo()
{
    return sLastRunDebugInfo;
}

void ALCrashReporter::foundCrashReport(std::string_view event_id, std::string_view crashed_run_id)
{
    // A second instance knows no previous run, so takes none.
    if (std::optional<PreviousReport> report = previousRunCrash(event_id, crashed_run_id, sLastRun.runId))
    {
        LLMutexLock lock(&sPreviousReportMutex);
        sPreviousReport = std::move(report);
    }
}

std::optional<ALCrashReporter::PreviousReport> ALCrashReporter::previousReport()
{
    LLMutexLock lock(&sPreviousReportMutex);
    return sPreviousReport;
}

std::optional<ALCrashReporter::LastRunEnd> ALCrashReporter::lastRunEnd()
{
    // A second instance's markers are the running first instance's.
    if (LLAppViewer::instance()->isSecondInstance())
    {
        return std::nullopt;
    }

    LastRunEnd end;
    switch (gLastExecEvent)
    {
        case LAST_EXEC_LOGOUT_FROZE:
            end.atLogout = true;
            [[fallthrough]];
        case LAST_EXEC_FROZE:
            end.kind = "freeze";
            break;
        case LAST_EXEC_LOGOUT_CRASH:
            end.atLogout = true;
            [[fallthrough]];
        case LAST_EXEC_LLERROR_CRASH:
        case LAST_EXEC_OTHER_CRASH:
        case LAST_EXEC_BAD_ALLOC:
            end.kind = "crash";
            break;
        default:
            // An unknown end is as often the task manager or a power cut.
            return std::nullopt;
    }

    if (const std::optional<PreviousReport> report = previousReport(); report && report->kind == end.kind)
    {
        end.eventId = report->eventId;
    }
    end.runId = sLastRun.runId;

    // About when it happened: when the run last wrote its log.
    const std::string log_file = LLAppViewer::previousLogFile();
    llstat status;
    if (!log_file.empty() && LLFile::stat(log_file, &status) == 0)
    {
        end.when = static_cast<F64>(status.st_mtime);
    }
    return end;
}
