/**
 * @file alscriptstudioaccount.h
 * @brief What of Script Studio's state is an account's own, and how it is kept apart from the shared.
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

#include "llsd.h"

// What of Script Studio's state is an account's own -- its tabs, the
// windows popped out, its pins, the scripts it opened lately -- kept with
// that account's settings, by grid, rather than in the state every account
// on the computer shares: one login's tabs are not the next's, and an alt
// sees none of another's. The window's shape and its switches stay shared.
namespace ALScriptStudioAccount
{
    // The account's own taken out of a state written whole, into `mine`:
    // each key it has moved over, each it has not taken out of `mine`.
    void split(LLSD& state, LLSD& mine);
    // A state to read: the shared one with the account's own put over it
    // where the account has kept any -- a key it has not kept taken out --
    // and as it was where it has kept none yet, which the first login
    // since these were shared takes over.
    LLSD merged(const LLSD& shared, const LLSD& mine);
}
