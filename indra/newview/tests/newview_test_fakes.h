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

namespace LL
{
    class WorkQueue;
}

namespace newview_test
{
    // The main loop's queue, as the viewer's is: made the first time a test
    // asks for it, never while the process starts, and kept for the rest of
    // the run, since what was posted to it for a test gone finds nobody. A
    // queue's name is its key in a process, so every test shares this one.
    LL::WorkQueue& mainLoop();
}

#endif // AL_NEWVIEW_TEST_FAKES_H
