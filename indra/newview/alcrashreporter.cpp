/**
 * @file alcrashreporter.cpp
 * @brief What every crash reporter files a report under, and the consent it starts on
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

#include "alcrashreporter.h"

#include "lldir.h"
#include "llfile.h"
#include "llsd.h"
#include "llstring.h"
#include "lluuid.h"
#include "v3math.h"

#if LL_WINDOWS
#include "llwin32headers.h"
#include <intrin.h>
#endif

#include <fmt/format.h>

#include <cerrno>
#include <cmath>
#include <cstdlib>

std::string ALCrashReporter::releaseName(S32 major, S32 minor, S32 patch, U64 build)
{
    return fmt::format("alchemy@{}.{}.{}+{}", major, minor, patch, build);
}

std::string ALCrashReporter::locationTag(std::string_view region, const LLVector3& position)
{
    return fmt::format("{}/{}/{}/{}", region,
                       std::lround(position.mV[VX]),
                       std::lround(position.mV[VY]),
                       std::lround(position.mV[VZ]));
}

const std::string& ALCrashReporter::runId()
{
    static const std::string id = LLUUID::generateNewID().asString();
    return id;
}

#if !AL_SENTRY
// Without a reporter the crash takes the usual unhandled route; on Windows
// as an exception every filter sees, since a fast-fail only Windows Error
// Reporting does.
void ALCrashReporter::fatal(std::string_view, const std::string&)
{
#if LL_WINDOWS
    RaiseException(FATAL_APP_EXIT, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#else
    std::abort();
#endif
}
#endif

std::string ALCrashReporter::consentSentinel()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "crash_reports_allowed");
}

bool ALCrashReporter::consentRecorded(const std::string& sentinel)
{
    return LLFile::isfile(sentinel);
}

void ALCrashReporter::recordConsent(const std::string& sentinel, bool allowed)
{
    if (allowed)
    {
        llofstream file(sentinel);
        file << "Crash reports may be sent. Preferences holds the answer; this file lets the reporter start before Preferences is read.\n";
    }
    else
    {
        LLFile::remove(sentinel, ENOENT);
    }
}

ALCrashReporter::PreviousRun ALCrashReporter::previousRun(const LLSD& info)
{
    PreviousRun run;
    run.runId = info["RunId"].asString();
    run.logFile = info["SLLog"].asString();
    run.userSettingsFile = info["SettingsFilename"].asString();
    run.accountSettingsFile = info["PerAccountSettingsFilename"].asString();
    run.agentName = info["LoginName"].asString();
    // The login name is written with the space replaced.
    LLStringUtil::replaceChar(run.agentName, '_', ' ');
    run.region = info["CurrentRegion"].asString();
    run.fatalMessage = info["FatalMessage"].asString();
    return run;
}
