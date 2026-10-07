/**
 * @file almasterclock.h
 * @brief How the disk masters wait: a callable run later, and the time now.
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

#pragma once

#include "stdtypes.h"

#include <functional>

// How the disk masters' units wait, the index and the watch: `after` runs
// a callable on this thread so many seconds from now, and `now` says the
// time, in seconds from any start. The viewer's frames by default
// (doAfterInterval, LLTimer); a test's own, so that it drives the time
// itself.
struct ALMasterClock
{
    std::function<void(std::function<void()> callable, F32 seconds)> after;
    std::function<F64()>                                             now;

    static ALMasterClock frames();
};
