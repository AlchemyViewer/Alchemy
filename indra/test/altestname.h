/**
 * @file altestname.h
 * @brief The name of the test case running, where a TUT test asked for its own.
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

#ifndef AL_ALTESTNAME_H
#define AL_ALTESTNAME_H

#include <string>

namespace ll_test
{
    // The running test case's name, as TUT's get_test_name() gave a test its
    // own: to describe what it starts, or to name a file it writes. Empty
    // outside a test case. Catch2's own way to ask is deprecated.
    const std::string& currentTestName();
}

#endif // AL_ALTESTNAME_H
