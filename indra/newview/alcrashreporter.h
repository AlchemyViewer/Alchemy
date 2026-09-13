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

#include <string>
#include <string_view>

class LLUUID;
class LLVector3;

// Engaged once the settings are read, so the opt-out is honoured, and closed
// last thing in cleanup. Where no reporter is built in, every entry point is
// a no-op, so the call sites carry no conditions.
namespace ALCrashReporter
{
    // The release a report files under: "alchemy@1.2.3+4567".
    std::string releaseName(S32 major, S32 minor, S32 patch, U64 build);

    // Where the agent stood, "Region/128/64/22", in whole metres.
    std::string locationTag(std::string_view region, const LLVector3& position);

#if AL_SENTRY && !LL_DARWIN
    bool init();
    void shutdown();
    bool isEngaged();
    void setUser(const LLUUID& id, const std::string& name);
    void setTag(std::string_view key, std::string_view value);
    void attach(const std::string& path);
    bool reportFreeze(const std::string& description);
    bool handleException(void* exception_pointers);
#else
    inline bool init() { return false; }
    inline void shutdown() {}
    inline bool isEngaged() { return false; }
    inline void setUser(const LLUUID&, const std::string&) {}
    inline void setTag(std::string_view, std::string_view) {}
    inline void attach(const std::string&) {}
    inline bool reportFreeze(const std::string&) { return false; }
    inline bool handleException(void*) { return false; }
#endif
}

#endif // AL_ALCRASHREPORTER_H
