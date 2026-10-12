/**
 * @file altestlogreplay.cpp
 * @brief LOGFAIL: a failed test case's log, replayed.
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

#include "altestlogreplay.h"
#include "altestname.h"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <iostream>
#include <memory>

namespace ll_test
{
    namespace
    {
        // Made by install(), and kept for the life of the process: LLError
        // holds it as a recorder until then.
        std::shared_ptr<LogReplay> sReplay;

        class LogReplayListener : public Catch::EventListenerBase
        {
        public:
            using Catch::EventListenerBase::EventListenerBase;

            // Partial: a test case with sections runs once per leaf section,
            // and each run fails or passes on its own.
            void testCasePartialStarting(Catch::TestCaseInfo const&, uint64_t) override
            {
                LogReplay::clear();
            }

            void testCasePartialEnded(Catch::TestCaseStats const& stats, uint64_t) override
            {
                if (stats.totals.assertions.failed > 0)
                {
                    LogReplay::replay(std::cerr, stats.testInfo->name);
                }
            }

            // A crash -- a signal, or on Windows a structured exception -- ends
            // the case without a testCasePartialEnded, at the moment its log
            // is wanted most.
            void fatalErrorEncountered(Catch::StringRef) override
            {
                LogReplay::replay(std::cerr, currentTestName(), true);
            }
        };
    }

    void LogReplay::install(LLError::ELevel level)
    {
        if (!sReplay)
        {
            sReplay = std::make_shared<LogReplay>();
            LLError::addRecorder(sReplay);
        }
        LLError::setDefaultLevel(level);
    }

    void LogReplay::clear()
    {
        if (sReplay)
        {
            std::lock_guard<std::mutex> lock(sReplay->mMutex);
            sReplay->mLines.clear();
        }
    }

    void LogReplay::replay(std::ostream& out, const std::string& test_case, bool after_crash)
    {
        if (!sReplay)
        {
            return;
        }
        std::unique_lock<std::mutex> lock(sReplay->mMutex, std::defer_lock);
        if (after_crash)
        {
            if (!lock.try_lock())
            {
                out << "---- log of \"" << test_case << "\" not replayed: it was being written ----"
                    << std::endl;
                return;
            }
        }
        else
        {
            lock.lock();
        }
        out << "---- log of \"" << test_case << "\" ----\n";
        for (const std::string& line : sReplay->mLines)
        {
            out << line << '\n';
        }
        out << "---- end of log ----" << std::endl;
    }

    void LogReplay::recordMessage(LLError::ELevel, const std::string& message)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        mLines.push_back(message);
    }
}

CATCH_REGISTER_LISTENER(ll_test::LogReplayListener)
