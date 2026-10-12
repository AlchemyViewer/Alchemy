/**
 * @file altestfatal.h
 * @brief What an LL_ERRS does in a Catch2 test: it throws, so the test case fails,
 *        or catches it if it expects one.
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

#ifndef AL_ALTESTFATAL_H
#define AL_ALTESTFATAL_H

#include <stdexcept>
#include <string>

namespace ll_test
{
    // Thrown by an LL_ERRS, or a failed llassert, in a test process. Uncaught,
    // its test case fails with the message; a test that means to provoke one
    // catches it. The TUT runner threw tut::failure the same way, and code
    // under test that catches and rethrows exceptions -- a coroutine's
    // top level, for one -- carries it to the test as it carried that.
    class FatalError : public std::runtime_error
    {
    public:
        explicit FatalError(const std::string& message);
    };

    // Makes every LL_ERRS from here on throw FatalError, on whichever thread
    // it happens. Off the main thread, on a thread of the test's own, nothing
    // catches it and the process ends through std::terminate; see
    // installTerminateHandler. An LLThread is different: outside Windows it
    // catches what its run() throws and posts it to the "mainloop" WorkQueue,
    // to be thrown again there, so a test of one sees its LL_ERRS only by
    // servicing that queue.
    void installFatalFunction();

    // Makes std::terminate, before it aborts, name the test case that was
    // running and what the exception in flight said. Catch2's own report of
    // the abort that follows names the case outside Windows, but not what was
    // thrown. On Windows, where Catch2 handles no SIGABRT and the CRT keeps a
    // terminate handler a thread, an abort from any thread names the case and
    // replays LOGFAIL's log, where an LL_ERRS wrote what went wrong.
    void installTerminateHandler();
}

#endif // AL_ALTESTFATAL_H
