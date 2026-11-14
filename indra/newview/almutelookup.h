/**
 * @file almutelookup.h
 * @brief ALMuteLookup: which of the mute list's mutes held by ID a lookup reaches
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

// The mute list keeps its mutes by ID in a set ordered by ID, and its mutes by name, which are written with the null
// ID, apart. A mute by ID can be of the null ID too, since the list keeps what the server sends. A lookup made for a
// mute by name, a group's among them, therefore never searches the mutes by ID, where the null ID would find that
// mute instead of the one by name.
struct ALMuteLookup
{
    // The mute by ID that key reaches in mutes, or mutes.end(): none for a key of the null ID looked up by name.
    template <class Mutes, class Mute>
    static auto findById(Mutes& mutes, const Mute& key) -> decltype(mutes.find(key))
    {
        if (key.mID.isNull() && key.mType == Mute::BY_NAME)
        {
            return mutes.end();
        }
        return mutes.find(key);
    }
};
