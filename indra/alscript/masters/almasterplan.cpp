/**
 * @file almasterplan.cpp
 * @brief What a send of a master file's script does, given the world's and the file's state.
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

#include "almasterplan.h"

// static
ALMasterPlan::Do ALMasterPlan::decide(Send kind, bool world_moved, bool world_same, bool unchanged, bool skip_unchanged)
{
    // Changed in the world, to something other than what would go up: a
    // real conflict.
    const bool conflict = world_moved && !world_same;
    if (kind == Send::Direct)
    {
        return conflict ? Do::SendKeepingTheirs : Do::Send;
    }
    if (conflict)
    {
        return Do::Hold;
    }
    // Changed in the world to just what would go up: the base moves, and
    // there is nothing to send, whatever the setting.
    if (world_moved)
    {
        return Do::Skip;
    }
    // The world as we left it: nothing to send where what would go up is
    // what went up, or what the world holds already.
    return skip_unchanged && (unchanged || world_same) ? Do::Skip : Do::Send;
}
