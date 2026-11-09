/**
 * @file alagentlistupdates.h
 * @brief ALAgentListUpdates: holding a chat session's roster updates that come before its speaker manager
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

// ChatterBoxSessionAgentListUpdates can arrive before the session it updates has a speaker manager, which then reads
// what was held for the session once it has. An update lists agents in either of two formats, or both: the
// "agent_updates" map, of each agent's transition and info, and the older "updates" map, of each agent's "ENTER" or
// "LEAVE". Each format is held whichever comes, the latest entry for an agent replacing the one before.
struct ALAgentListUpdates
{
    // Lays update over held, a map of the two formats.
    static void hold(LLSD& held, const LLSD& update)
    {
        for (const char* format : { "agent_updates", "updates" })
        {
            if (!update.has(format))
            {
                continue;
            }
            const LLSD& entries = update[format];
            if (!entries.isMap())
            {
                continue;
            }
            for (LLSD::map_const_iterator it = entries.beginMap(); it != entries.endMap(); ++it)
            {
                held[format][it->first] = it->second;
            }
        }
    }
};
