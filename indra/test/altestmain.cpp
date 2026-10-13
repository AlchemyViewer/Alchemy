/**
 * @file altestmain.cpp
 * @brief The main of every Catch2 test binary: it reads Catch2's command line and
 *        ours, sets up what LL code expects of a process, runs the tests, and
 *        ends the binary's process-wide fixtures in order.
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

#include "altestfatal.h"
#include "altesthelper.h"
#include "altestlogreplay.h"
#include "alteststateguard.h"
#include "altestteardown.h"

#include "llapp.h"
#include "llapr.h"
#include "llerrorcontrol.h"
#include "llprocessor.h"
#include "llstring.h"
#include "lltracethreadrecorder.h"

#include <catch2/catch_session.hpp>

#include <cstdlib>
#include <iostream>

namespace
{
    // Whether the host can run a binary compiled for an x86-64 level. A level
    // of 0 is the baseline, which anything the viewer builds for can run.
    bool hostRunsIsaLevel(int level)
    {
        if (level <= 0)
        {
            return true;
        }
        LLProcessorInfo cpu;
        switch (level)
        {
            case 2: return cpu.hasSSE42();
            case 3: return cpu.hasAVX2();
            case 4: return cpu.hasAVX512F();
            default: return false;
        }
    }

    // LOGTEST logs everything at its level to stderr as it happens. Otherwise
    // LL code logs at DEBUG to nowhere but the --al-log file and, with LOGFAIL,
    // to a buffer replayed for a test case that fails. Either way, LLError is
    // set up as LL code expects to find it.
    void setUpLogging(const char* executable, bool log_to_file)
    {
        const char* logtest = std::getenv("LOGTEST");
        if (logtest && *logtest)
        {
            LLError::initForApplication(".", ".", true /* log to stderr */);
            LLError::setDefaultLevel(LLError::decodeLevel(logtest));
        }
        else
        {
            LLError::initForApplication(".", ".", false /* do not log to stderr */);
            LLError::setDefaultLevel(LLError::LEVEL_DEBUG);
            const char* logfail = std::getenv("LOGFAIL");
            if (logfail && *logfail)
            {
                ll_test::LogReplay::install(LLError::decodeLevel(logfail));
            }
        }
        if (log_to_file)
        {
            // One file to a process: CTest runs a binary's test files side by
            // side, each in a process of its own.
            LLError::logToFile(llformat("%s.%d.log", executable, LLApp::getPid()));
        }
    }

    // What a test run and a helper both find: APR, LLError as LL code
    // expects it, LL_ERRS throwing, and LLTrace's master recorder.
    void setUpProcess(const char* executable, bool log_to_file)
    {
        ll_init_apr();
        setUpLogging(executable, log_to_file);
        ll_test::installFatalFunction();
        ll_test::installTerminateHandler();

        // LLTrace needs a master recorder before any thread records. Never
        // freed, as the TUT runner never freed it: static destructors that
        // record would find it gone.
        LLTrace::set_master_thread_recorder(new LLTrace::ThreadRecorder());
    }
}

int main(int argc, char* argv[])
{
    // Tracy first, so it allocates before anything else does
    // (https://github.com/wolfpld/tracy/issues/196).
    LL_PROFILER_FRAME_END;
    LL_PROFILER_SET_THREAD_NAME("App");

    // A helper a test launched from this binary runs alone, and Catch2 never
    // sees its argument.
    if (const char* helper = ll_test::Helper::named(argc, argv))
    {
        setUpProcess(argv[0], false);
        const int result = ll_test::Helper::run(helper);
        ll_test::Teardown::runAll();
        ll_cleanup_apr();
        return result;
    }

    Catch::Session session;

    // Long names only: Catch2 owns every short one.
    int isa_level = 0;
    bool log_to_file = false;
    using namespace Catch::Clara;
    session.cli(session.cli()
                | Opt(isa_level, "level")["--isa-level"](
                      "exit 125, which CTest reads as skipped, unless the host runs this x86-64 "
                      "level: 2 for SSE4.2, 3 for AVX2, 4 for AVX-512")
                | Opt(log_to_file)["--al-log"](
                      "also log to <executable>.<pid>.log, at the level LOGTEST or LOGFAIL "
                      "names, else DEBUG"));

    // Declared order, as TUT ran a group's tests, until no test leans on the
    // ones before it. --order on the command line still wins.
    session.configData().runOrder = Catch::TestRunOrder::Declared;
    // Every case tagged with its file's name, [#<file stem>], as
    // --filenames-as-tags tags them: a test file's run names its file with
    // one tag, and a filter of several files is one argument.
    session.configData().filenamesAsTags = true;

    if (const int result = session.applyCommandLine(argc, argv); result != 0)
    {
        return result;
    }

    // A listing, help or the like runs no test, and needs nothing set up: CTest
    // and tools list a binary that may not be able to run its tests here.
    const Catch::ConfigData& config = session.configData();
    if (config.showHelp || config.libIdentify || config.listTests || config.listTags
        || config.listReporters || config.listListeners)
    {
        return session.run();
    }

    if (!hostRunsIsaLevel(isa_level))
    {
        std::cout << "Skipped: this host does not run x86-64 level " << isa_level << std::endl;
        return 125;
    }

    setUpProcess(argv[0], log_to_file);
    ll_test::StateGuard::addCommonChecks();

    int result = session.run();

    const int teardown_failures = ll_test::Teardown::runAll();
    ll_cleanup_apr();

    // A run that failed no case can still fail: a case left process state
    // changed, or a fixture's end found something wrong. Skipped cases ran
    // too, as far as they got, so a run of nothing but skips is held to the
    // same.
    if (result == 0 || result == Catch::AllTestsSkippedExitCode)
    {
        if (ll_test::StateGuard::changes() > 0)
        {
            std::cerr << "The state guard found " << ll_test::StateGuard::changes()
                      << " change(s) a test case left behind; see above." << std::endl;
            result = 1;
        }
        if (teardown_failures > 0)
        {
            std::cerr << teardown_failures << " teardown step(s) threw; see above." << std::endl;
            result = 1;
        }
    }
    // Every test file is a CTest entry of its own, all with 125 as their skip
    // code; Catch2 says "all skipped" with 4.
    if (result == Catch::AllTestsSkippedExitCode)
    {
        result = 125;
    }
    return result;
}
