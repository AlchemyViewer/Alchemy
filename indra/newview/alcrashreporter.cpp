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

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <stdexcept>

std::string ALCrashReporter::releaseName(S32 major, S32 minor, S32 patch, U64 build)
{
    return fmt::format("alchemy@{}.{}.{}+{}", major, minor, patch, build);
}

std::string ALCrashReporter::locationTag(std::string_view region, const LLVector3& position)
{
    // Room for the region and three signed 64-bit integers after slashes.
    std::string tag(region.size() + 3 * 21 + 1, '\0');
    tag.resize(locationTag(tag.data(), tag.size(), region, position));
    return tag;
}

size_t ALCrashReporter::locationTag(char* buffer, size_t size, std::string_view region, const LLVector3& position)
{
    if (size == 0)
    {
        return 0;
    }
    char* out = buffer;
    char* const end = buffer + size - 1;
    auto append = [&](const char* text, size_t length)
    {
        length = std::min(length, static_cast<size_t>(end - out));
        memcpy(out, text, length);
        out += length;
    };

    append(region.data(), region.size());
    for (S32 axis : { VX, VY, VZ })
    {
        char metres[24] = { '/' };
        const auto result = std::to_chars(metres + 1, metres + sizeof(metres), std::lround(position.mV[axis]));
        append(metres, result.ptr - metres);
    }
    *out = '\0';
    return out - buffer;
}

const std::string& ALCrashReporter::runId()
{
    static const std::string id = LLUUID::generateNewID().asString();
    return id;
}

#ifndef LL_RELEASE_FOR_DOWNLOAD
void ALCrashReporter::probe(std::string_view point)
{
    const char* named = getenv("AL_CRASH_PROBE");
    if (!named)
    {
        return;
    }
    const std::string_view named_point(named);
    constexpr std::string_view THROW_SUFFIX = ":throw";
    if (named_point == point)
    {
        // A wild write, as the viewer's own crashes are, rather than a crash
        // the reporter is handed.
        volatile S32* nowhere = nullptr;
        *nowhere = 0;
    }
    else if (named_point.size() == point.size() + THROW_SUFFIX.size() && named_point.starts_with(point)
             && named_point.ends_with(THROW_SUFFIX))
    {
        throw std::runtime_error(fmt::format("AL_CRASH_PROBE {}", named_point));
    }
}
#endif

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

ALCrashReporter::Consent ALCrashReporter::consentFromSetting(S32 value)
{
    switch (value)
    {
        case static_cast<S32>(Consent::Allowed):
            return Consent::Allowed;
        case static_cast<S32>(Consent::Declined):
            return Consent::Declined;
        default:
            return Consent::Unasked;
    }
}

namespace
{
    constexpr const char* ALLOWED_FILE = "crash_reports_allowed";
    constexpr const char* DECLINED_FILE = "crash_reports_declined";
}

std::string ALCrashReporter::consentDirectory()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_USER_SETTINGS, "");
}

ALCrashReporter::Consent ALCrashReporter::recordedConsent(const std::string& directory)
{
    const bool allowed = LLFile::isfile(gDirUtilp->add(directory, ALLOWED_FILE));
    const bool declined = LLFile::isfile(gDirUtilp->add(directory, DECLINED_FILE));
    if (allowed == declined)
    {
        return Consent::Unasked;
    }
    return allowed ? Consent::Allowed : Consent::Declined;
}

void ALCrashReporter::recordConsent(const std::string& directory, Consent consent)
{
    const std::string allowed = gDirUtilp->add(directory, ALLOWED_FILE);
    const std::string declined = gDirUtilp->add(directory, DECLINED_FILE);

    // The other answer goes first: stopped between the two, the record reads
    // as unasked, which sends nothing.
    if (consent != Consent::Allowed)
    {
        LLFile::remove(allowed, ENOENT);
    }
    if (consent != Consent::Declined)
    {
        LLFile::remove(declined, ENOENT);
    }

    if (consent == Consent::Allowed)
    {
        llofstream file(allowed);
        file << "Crash reports may be sent. Preferences holds the answer; this file lets the reporter start before Preferences is read.\n";
    }
    else if (consent == Consent::Declined)
    {
        llofstream file(declined);
        file << "Crash reports may not be sent. Preferences holds the answer; this file lets the reporter stay off before Preferences is read.\n";
    }
}

std::string ALCrashReporter::compactEventId(std::string_view id)
{
    std::string hex;
    hex.reserve(32);
    for (char c : id)
    {
        if (c != '-' && c != '{' && c != '}')
        {
            hex += c;
        }
    }
    LLStringUtil::toLower(hex);
    if (hex.size() != 32 || hex.find_first_not_of("0123456789abcdef") != std::string::npos
        || hex.find_first_not_of('0') == std::string::npos)
    {
        return std::string();
    }
    return hex;
}

std::string ALCrashReporter::reportRecordFile()
{
    return gDirUtilp->getExpandedFilename(LL_PATH_LOGS, "last_report");
}

void ALCrashReporter::recordReport(const std::string& record_file, const PreviousReport& report)
{
    // Written beside the record under a name of its own, then renamed over
    // it: whatever stops the write, or writes alongside it, the record is
    // one whole line.
    const std::string line = report.kind + " " + report.eventId + "\n";
    const std::string written = fmt::format("{}.{}.tmp", record_file, LLUUID::generateNewID().asString());
    bool whole = false;
    {
        std::error_code ec;
        LLFile file(written, LLFile::out | LLFile::trunc | LLFile::binary, ec);
        whole = !ec && file.write(line.data(), static_cast<S64>(line.size()), ec) == static_cast<S64>(line.size())
                && file.close(ec) == 0;
    }
    if (!whole || LLFile::rename(written, record_file) != 0)
    {
        LLFile::remove(written, ENOENT);
    }
}

std::optional<ALCrashReporter::PreviousReport> ALCrashReporter::takeRecordedReport(const std::string& record_file)
{
    if (!LLFile::isfile(record_file))
    {
        return std::nullopt;
    }
    std::string line = LLFile::getContents(record_file);
    LLFile::remove(record_file, ENOENT);

    LLStringUtil::trim(line);
    const size_t space = line.find(' ');
    if (space == std::string::npos)
    {
        return std::nullopt;
    }
    PreviousReport report;
    report.kind = line.substr(0, space);
    report.eventId = compactEventId(std::string_view(line).substr(space + 1));
    if ((report.kind != "crash" && report.kind != "freeze") || report.eventId.empty())
    {
        return std::nullopt;
    }
    return report;
}

std::optional<ALCrashReporter::PreviousReport> ALCrashReporter::previousRunCrash(std::string_view event_id,
                                                                                 std::string_view crashed_run_id,
                                                                                 std::string_view previous_run_id)
{
    const std::string previous_run = compactEventId(previous_run_id);
    std::string id = compactEventId(event_id);
    if (previous_run.empty() || id.empty() || compactEventId(crashed_run_id) != previous_run)
    {
        return std::nullopt;
    }
    return PreviousReport{ "crash", std::move(id) };
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
