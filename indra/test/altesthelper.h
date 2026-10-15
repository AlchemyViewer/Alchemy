/**
 * @file altesthelper.h
 * @brief A process a test launches from its own binary, run by name and
 *        without the binary's test cases.
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

#ifndef AL_ALTESTHELPER_H
#define AL_ALTESTHELPER_H

#include <string>

namespace ll_test
{
    // A test that needs a child process of its own making -- one that owns
    // other processes, say, and then exits -- can launch its own binary with
    // Helper::argument(name). The main then runs only the helper of that
    // name, as LL code expects to find a process set up, and exits with what
    // it returns: no test case runs, so a binary whose files share a process
    // does not run its whole suite again in the child. The argument is the
    // main's alone; Catch2 never sees it.
    //
    // A helper is registered as a test case is, by AL_TEST_HELPER(name) in
    // the test file that launches it. Names are unique within a binary.
    class Helper
    {
    public:
        using Main = int (*)();

        // AL_TEST_HELPER's: a second helper of one name aborts the process,
        // as it starts.
        Helper(const char* name, Main main);

        // What to pass the binary to run the helper of that name.
        static std::string argument(const std::string& name);

        // The main's: the helper argv names, or null when it names none.
        static const char* named(int argc, char* argv[]);

        // The main's: runs the helper of that name and returns its exit code,
        // or reports that there is none and returns 2.
        static int run(const std::string& name);
    };
}

// Defines the helper `name`, a function returning the helper process's exit
// code, and registers it:
//     AL_TEST_HELPER(llprocess_autokill) { ...; return 0; }
#define AL_TEST_HELPER(name)                                                       \
    static int al_test_helper_##name();                                            \
    static const ll_test::Helper al_test_helper_registration_##name(#name,         \
                                                                    &al_test_helper_##name); \
    static int al_test_helper_##name()

#endif // AL_ALTESTHELPER_H
