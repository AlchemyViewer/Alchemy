/**
 * @file alteststateguard.cpp
 * @brief Process state every test case must leave as it found it.
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

#include "alteststateguard.h"
#include "altestfatal.h"

#include "llapp.h"
#include "llapr.h"
#include "llerrorcontrol.h"
#include "lltracethreadrecorder.h"

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <algorithm>
#include <clocale>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <set>
#include <sstream>
#include <vector>

#if LL_WINDOWS
#include "llwin32headers.h"
#include <cwctype>
#include <float.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif

#if !LL_WINDOWS
extern char** environ;
#endif

namespace ll_test
{
    namespace
    {
        struct Check
        {
            std::string name;
            StateGuard::Describe describe;
            StateGuard::Restore restore;
            std::string last;
        };

        std::vector<Check>& checks()
        {
            static std::vector<Check> sChecks;
            return sChecks;
        }

        int sChanges = 0;

        std::string pointerText(const void* pointer)
        {
            std::ostringstream out;
            out << pointer;
            return out.str();
        }

        // A multi-line description is reported by its lines that went and
        // came, not as two walls of text.
        void reportDifference(std::ostream& out, const std::string& was, const std::string& now)
        {
            if (was.find('\n') == std::string::npos && now.find('\n') == std::string::npos)
            {
                out << "    was: " << was << "\n    now: " << now << "\n";
                return;
            }
            auto lines = [](const std::string& text)
            {
                std::set<std::string> result;
                std::istringstream in(text);
                for (std::string line; std::getline(in, line);)
                {
                    result.insert(line);
                }
                return result;
            };
            const std::set<std::string> before = lines(was);
            const std::set<std::string> after = lines(now);
            for (const std::string& line : before)
            {
                if (!after.count(line))
                {
                    out << "    gone: " << line << "\n";
                }
            }
            for (const std::string& line : after)
            {
                if (!before.count(line))
                {
                    out << "    new:  " << line << "\n";
                }
            }
        }

        std::string workingDirectory()
        {
            std::error_code error;
            const std::filesystem::path path = std::filesystem::current_path(error);
            return error ? "(unreadable: " + error.message() + ")" : path.string();
        }

        // Sorted, one variable a line, so a change reads as the lines it made.
        // Not Windows' hidden =C:=C:\dir variables, the directory last made
        // current on each drive: a change of directory makes one for its
        // drive, as a font or GL driver lookup on the system drive does from
        // a tree on another, and the working directory has its own check.
        // Nor _NT_SYMBOL_PATH, dbghelp's: Tracy's profiler thread appends
        // the executable's folder to it as it starts (TracyCallstack.cpp),
        // on its own time, so whichever case was running when it did would
        // be blamed for it.
        std::string environment()
        {
            std::vector<std::string> variables;
#if LL_WINDOWS
            if (wchar_t* block = GetEnvironmentStringsW())
            {
                for (const wchar_t* entry = block; *entry; entry += wcslen(entry) + 1)
                {
                    if (entry[0] == L'=' && std::iswalpha(entry[1]) && entry[2] == L':')
                    {
                        continue;
                    }
                    if (_wcsnicmp(entry, L"_NT_SYMBOL_PATH=", 16) == 0)
                    {
                        continue;
                    }
                    variables.push_back(ll_convert_wide_to_string(entry));
                }
                FreeEnvironmentStringsW(block);
            }
#else
            for (char** entry = environ; entry && *entry; ++entry)
            {
                variables.emplace_back(*entry);
            }
#endif
            std::sort(variables.begin(), variables.end());
            std::string text;
            for (const std::string& variable : variables)
            {
                text += variable;
                text += '\n';
            }
            return text;
        }

        // The control state only: the sticky exception flags change with any
        // arithmetic and say nothing about what a test left behind.
        std::string floatingPointControl()
        {
            std::ostringstream out;
            out << std::hex;
#if LL_WINDOWS
            out << "control 0x" << _controlfp(0, 0);
#elif defined(__aarch64__)
            std::uint64_t fpcr;
            __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
            out << "FPCR 0x" << fpcr;
#elif defined(__x86_64__) || defined(__i386__)
            out << "MXCSR 0x" << (_mm_getcsr() & ~0x3Fu);
#else
            out << "unknown";
#endif
            return out.str();
        }

        std::string cLocale()
        {
            const char* locale = std::setlocale(LC_ALL, nullptr);
            return locale ? locale : "(none)";
        }

        // A function's target, where it is a plain function, as an address;
        // otherwise its type, which is what an override is told apart by.
        std::string fatalFunction()
        {
            const LLError::FatalFunction function = LLError::getFatalFunction();
            if (!function)
            {
                return "(none)";
            }
            using Plain = void (*)(const std::string&);
            if (const Plain* plain = function.target<Plain>())
            {
                return pointerText(reinterpret_cast<const void*>(*plain));
            }
            return function.target_type().name();
        }

        // Catch2 hands every event to every listener; this one runs the
        // guard's checks around each test case.
        class StateGuardListener : public Catch::EventListenerBase
        {
        public:
            using Catch::EventListenerBase::EventListenerBase;

            void testRunStarting(Catch::TestRunInfo const&) override
            {
                StateGuard::snapshot();
            }

            void testCaseEnded(Catch::TestCaseStats const& stats) override
            {
                StateGuard::check(stats.testInfo->name);
            }
        };
    }

    void StateGuard::add(std::string name, Describe describe, Restore restore)
    {
        std::string first = describe();
        checks().push_back({ std::move(name), std::move(describe), std::move(restore), std::move(first) });
    }

    void StateGuard::addCommonChecks()
    {
        add("the working directory", workingDirectory);
        add("the environment", environment);
        add("the floating-point control state", floatingPointControl);
        add("the C locale", cLocale);
        add("the APR pool", [] { return pointerText(gAPRPoolp); });
        add("the LLTrace master recorder", [] { return pointerText(LLTrace::get_master_thread_recorder()); });
        add("the LLApp", [] { return pointerText(LLApp::instance()); });
        add("LLError's fatal function", fatalFunction);
    }

    void StateGuard::snapshot()
    {
        for (Check& check : checks())
        {
            check.last = check.describe();
        }
    }

    void StateGuard::accept(const std::string& check_name)
    {
        for (Check& check : checks())
        {
            if (check_name.empty() || check.name == check_name)
            {
                check.last = check.describe();
            }
        }
    }

    void StateGuard::check(const std::string& test_case)
    {
        for (Check& check : checks())
        {
            std::string now = check.describe();
            if (now == check.last)
            {
                continue;
            }
            ++sChanges;
            std::cerr << "State guard: test case \"" << test_case << "\" left " << check.name
                      << " changed\n";
            reportDifference(std::cerr, check.last, now);
            if (check.restore)
            {
                check.restore();
                now = check.describe();
            }
            check.last = std::move(now);
        }
        std::cerr << std::flush;
    }

    int StateGuard::changes()
    {
        return sChanges;
    }
}

CATCH_REGISTER_LISTENER(ll_test::StateGuardListener)
