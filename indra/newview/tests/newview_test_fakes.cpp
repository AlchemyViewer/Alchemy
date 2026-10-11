/**
 * @file newview_test_fakes.cpp
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

#include "linden_common.h"

#include "newview_test_fakes.h"

#include "workqueue.h"

#include <string>

// An object, linked whole into each test that names it, rather than an
// archive's member, so each test binary has one definition of each.

// llui reaches the viewer for this one, and linking any of the library pulls
// the object that calls it. Nothing under test goes near it. A linker that
// reads its archives once, in order, would have passed an archive listed
// before llui by the time llui asked.
class LLAvatarName;
const std::string& rlvGetAnonym(const LLAvatarName&)
{
    static const std::string anon("Anon");
    return anon;
}

LL::WorkQueue& newview_test::mainLoop()
{
    static LL::WorkQueue queue("mainloop", 1024);
    return queue;
}
