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
#include <string>
#include <string_view>
#include <utility>

class LLSD;
class LLUUID;
class LLVector3;

// Engaged from the top of the viewer's init, on a consent it can read before
// the settings exist, and closed last thing in cleanup. Where no reporter is
// built in, every entry point is a no-op, so the call sites carry no
// conditions.
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

    // Where the agent stood, "Region/128/64/22", in whole metres.
    std::string locationTag(std::string_view region, const LLVector3& position);

    // One id per run, on every report this run sends and in its static debug
    // file, so a report filed on the next launch can be joined to the crash.
    const std::string& runId();

    // Consent is a sentinel file in the user's settings directory: present
    // means reports may be sent. It is what lets the reporter start before
    // the settings are readable; once they are, they refresh it.
    std::string consentSentinel();
    bool consentRecorded(const std::string& sentinel);
    void recordConsent(const std::string& sentinel, bool allowed);

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

    // How the viewer dies on purpose: LL_ERRS and std::terminate end here.
    // The kind and the message ride the report as its fatal context, and
    // the crash is one the reporter is sure to see: on Windows the
    // exception abort() raised before fast-fail existed, handed to the
    // reporter directly, and abort() itself elsewhere.
    [[noreturn]] void fatal(std::string_view kind, const std::string& message);
    inline constexpr U32 FATAL_APP_EXIT = 0x40000015; // STATUS_FATAL_APP_EXIT

#if AL_SENTRY
    // Engages if the sentinel allows it.
    bool init();
    // The settings' answer, once readable and whenever it changes: recorded,
    // and the reporter engaged or closed to match.
    void refreshConsent(bool allowed);
    void shutdown();
    bool isEngaged();
    void setUser(const LLUUID& id, const std::string& name);
    void setTag(std::string_view key, std::string_view value);
    // A named group of values shown together on every report from here on.
    void setContext(std::string_view name,
                    std::initializer_list<std::pair<std::string_view, std::string_view>> values);
    void attach(const std::string& path);
    bool reportFreeze(const std::string& description);
    bool handleException(void* exception_pointers);
#else
    inline bool init() { return false; }
    inline void refreshConsent(bool allowed) { recordConsent(consentSentinel(), allowed); }
    inline void shutdown() {}
    inline bool isEngaged() { return false; }
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
