/**
 * @file alteststateguard.h
 * @brief Process state every test case must leave as it found it, checked after
 *        each case, so a leak fails the test that made it.
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

#ifndef AL_ALTESTSTATEGUARD_H
#define AL_ALTESTSTATEGUARD_H

#include <functional>
#include <string>

namespace ll_test
{
    // Each check describes one piece of process state as text. The guard takes
    // every description when the run starts and again after each test case;
    // where one differs, it reports the case that changed it, then takes the
    // new description as the one to compare with, so only that case is named.
    // A check with a restore puts the state back after reporting, so a known
    // leak cannot reach the next case.
    //
    // Today a binary runs one test file per process, so the guard catches what
    // would otherwise only surface once the files share a process: in the
    // combined run, as some later test failing for no reason of its own.
    class StateGuard
    {
    public:
        using Describe = std::function<std::string()>;
        using Restore = std::function<void()>;

        // Takes the check's first description as it is added. A library's
        // own checks are added by a listener of its own when the run starts,
        // before or after the guard's snapshot there: either way, no case is
        // compared with nothing.
        static void add(std::string name, Describe describe, Restore restore = {});

        // What every LL process has: the working directory, the environment,
        // the floating-point control state, the C locale, the APR pool, the
        // LLTrace master recorder, the LLApp and LLError's fatal function.
        static void addCommonChecks();

        // The listener's: before the first case, and after each.
        static void snapshot();
        static void check(const std::string& test_case);

        // A process-wide fixture's, once it is made (see Teardown): what it
        // changed on purpose -- a variable it set, the LLApp it made, the
        // floating-point mode a GL driver chose -- becomes the state to
        // compare with, so the test case that first asked for the fixture is
        // not blamed for it. Names one check, as add() named it ("the
        // environment"), or every check when empty. Whatever that case had
        // itself changed of the same state is accepted with it.
        static void accept(const std::string& check_name = {});

        // How many times a case left something changed. The main fails the run
        // if any did, since a listener cannot fail a test case of its own.
        static int changes();
    };
}

#endif // AL_ALTESTSTATEGUARD_H
