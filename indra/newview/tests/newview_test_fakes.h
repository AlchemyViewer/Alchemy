/**
 * @file newview_test_fakes.h
 * @brief What newview's tests take of the viewer, faked once for every test that links it.
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

#ifndef AL_NEWVIEW_TEST_FAKES_H
#define AL_NEWVIEW_TEST_FAKES_H

#include "alrecoverystore.h"
#include "alteststateguard.h"
#include "altestteardown.h"

namespace LL
{
    class WorkQueue;
}

namespace newview_test
{
    // A queue for a test to stand in for the main loop's, chosen through
    // ALMainLoop::Choice for as long as the test wants work handed back to
    // it: made the first time a test asks for it, never while the process
    // starts, and kept for the rest of the run, since what was posted to it
    // for a test gone finds nobody. Not under the main loop's name, so that
    // its being made chooses nothing for the tests after.
    LL::WorkQueue& mainLoop();

    // The thread Script Studio's recovery stores write on, ALRecoveryWriter's,
    // the process's, which the viewer stops in its cleanup: made by the first
    // fixture that makes a store, before it does, as the store would have
    // made it when each file had a process of its own, and stopped by the
    // harness's teardown, so that it then runs is no change a case made.
    inline void useRecoveryWriter()
    {
        static const bool made = []
        {
            ALRecoveryWriter::getInstance();
            ll_test::Teardown::add([] { ALRecoveryWriter::deleteSingleton(); });
            ll_test::StateGuard::accept("the live thread pools");
            ll_test::StateGuard::accept("the live work queues");
            return true;
        }();
        (void)made;
    }
}

#endif // AL_NEWVIEW_TEST_FAKES_H
