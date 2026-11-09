/**
 * @file alfriendbacklog.h
 * @brief ALFriendBacklog: what the circuit says of residents not yet in the friends list, held until they may be
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

#include "lluuid.h"

#include <utility>
#include <vector>

// The friends tracker listens to the circuit before the login's friends list is applied, so a notice can name a
// friend it has not heard of yet. Such notices are held until the list is applied and then handed back in the order
// they came, to be applied to whoever the list made a friend. Once the list is applied, a notice about a resident
// not in it names someone who is not a friend, and is not held.
class ALFriendBacklog
{
public:
    enum class Kind : U8
    {
        ONLINE,  // OnlineNotification
        OFFLINE, // OfflineNotification
    };

    struct Notice
    {
        LLUUID mID;
        Kind   mKind = Kind::ONLINE;
    };

    // Holds a notice about a resident not in the friends list, while the list may yet name them. Says whether it
    // was held.
    bool hold(const Notice& notice)
    {
        if (mListApplied)
        {
            return false;
        }
        mHeld.push_back(notice);
        return true;
    }

    // The login's friends list has been applied: hands back what was held until now, in the order it came, and
    // holds nothing from now on.
    std::vector<Notice> applyList()
    {
        mListApplied = true;
        return std::exchange(mHeld, {});
    }

    bool isListApplied() const { return mListApplied; }
    size_t heldCount() const { return mHeld.size(); }

private:
    std::vector<Notice> mHeld;
    bool                mListApplied = false;
};
