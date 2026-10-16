/**
 * @file alscriptstudioaccount.cpp
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


#include "llviewerprecompiledheaders.h"

#include "alscriptstudioaccount.h"

namespace ALScriptStudioAccount
{
    namespace
    {
        const char* const KEYS[] = { "open", "windows", "pinned", "recent_scripts" };
    }

    void split(LLSD& state, LLSD& mine)
    {
        if (!mine.isMap())
        {
            mine = LLSD::emptyMap();
        }
        for (const char* key : KEYS)
        {
            if (state.has(key))
            {
                mine[key] = state[key];
                state.erase(key);
            }
            else
            {
                mine.erase(key);
            }
        }
    }

    LLSD merged(const LLSD& shared, const LLSD& mine)
    {
        LLSD state = shared;
        if (!mine.isMap())
        {
            return state;
        }
        for (const char* key : KEYS)
        {
            if (mine.has(key))
            {
                state[key] = mine[key];
            }
            else
            {
                state.erase(key);
            }
        }
        return state;
    }
}
