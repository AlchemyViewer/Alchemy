/**
 * @file llui_test_fakes.cpp
 * @brief What llui's tests link in place of the viewer
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Second Life Viewer Source Code
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

// One definition each of what llui asks the viewer for, linked into every
// test and benchmark that links the library. A test file holds none of its
// own: two copies in one binary would not link.

#include "linden_common.h"

#include <string>

class LLAvatarName;

// llurlentry.cpp asks RLVa for the name an anonymised avatar is shown under,
// and linking any of the library pulls the object that asks. Nothing under
// test goes near it.
const std::string& rlvGetAnonym(const LLAvatarName&)
{
    static const std::string anon("Anon");
    return anon;
}
