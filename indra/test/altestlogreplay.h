/**
 * @file altestlogreplay.h
 * @brief LOGFAIL: what LL code logged while a test case ran, kept, and printed only
 *        if the case failed.
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

#ifndef AL_ALTESTLOGREPLAY_H
#define AL_ALTESTLOGREPLAY_H

#include "llerrorcontrol.h"

#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace ll_test
{
    // LLError's stderr recorder writes with fprintf, which Catch2's own output
    // capture, a swap of std::cout's and std::cerr's buffers, never sees. So
    // the log is kept by a recorder of its own: cleared as each test case
    // starts, printed to stderr if it failed, and dropped if it passed.
    class LogReplay : public LLError::Recorder
    {
    public:
        // From the main, once, when LOGFAIL names a level: everything at or
        // above it is kept, and LLError's default level becomes it.
        static void install(LLError::ELevel level);

        // The listener's, around each test case. Both do nothing when LOGFAIL
        // was not set. After a crash, the thread that crashed may hold the
        // log as it writes to it: then a replay says so rather than wait.
        static void clear();
        static void replay(std::ostream& out, const std::string& test_case, bool after_crash = false);

        void recordMessage(LLError::ELevel level, const std::string& message) override;

    private:
        std::mutex mMutex;
        std::vector<std::string> mLines;
    };
}

#endif // AL_ALTESTLOGREPLAY_H
