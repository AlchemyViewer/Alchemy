/**
 * @file altestteardown.h
 * @brief The end of a test binary's process-wide fixtures, in order, before the
 *        main lets LLError and APR go.
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

#ifndef AL_ALTESTTEARDOWN_H
#define AL_ALTESTTEARDOWN_H

#include <functional>

namespace ll_test
{
    // A process-wide fixture -- a GL context, a headless UI, a messaging
    // system -- is made by its accessor the first time a test asks for it,
    // and registers its end here. The main runs the ends after the last test
    // case, newest first, while APR and LLError are still up. Left to static
    // destruction, they would run in whatever order the linker chose, after
    // the main had already let APR go. A fixture that changes process state
    // on purpose as it is made tells the state guard (StateGuard::accept).
    class Teardown
    {
    public:
        static void add(std::function<void()> step);

        // The main's: runs each step once, newest first. A step that throws
        // is reported and the rest still run; the count of those that threw
        // is returned, and the main fails the run on any, as a check at
        // static destruction ended a TUT process.
        static int runAll();
    };
}

#endif // AL_ALTESTTEARDOWN_H
