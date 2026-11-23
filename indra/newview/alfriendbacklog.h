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

#include <algorithm>
#include <utility>
#include <vector>

// The friends tracker listens to the circuit before the login's friends list is applied, so a notice can name a
// friend it has not heard of yet: that they are online, that the friendship has ended, that rights have changed.
// The list was made at login, before anything the circuit says, so such notices are held until the list is applied
// and then handed back in the order they came, to be applied over it to whoever it made a friend. A friendship formed
// before then, as by an acceptance read from the offline messages, is held in turn with them, so that it does not
// stand in for the list's own entry for that friend. Once the list is applied, a notice about a resident not in it
// names someone who is not a friend, and is not held, unless the agent has accepted their friendship offer: the
// server may tell of the new friend before it answers the acceptance, so notices about them are held until it
// answers.
class ALFriendBacklog
{
public:
    enum class Kind : U8
    {
        ONLINE,      // OnlineNotification
        OFFLINE,     // OfflineNotification
        TERMINATED,  // TerminateFriendship
        RIGHTS_TO,   // ChangeUserRights: the rights the agent grants the friend
        RIGHTS_FROM, // ChangeUserRights: the rights the friend grants the agent
        FORMED,      // the resident became a friend
    };

    struct Notice
    {
        LLUUID mID;
        Kind   mKind = Kind::ONLINE;
        S32    mRights = 0; // for RIGHTS_TO and RIGHTS_FROM
    };

    // Holds a notice about a resident not in the friends list, while they may yet be in it. Says whether it was held.
    bool hold(const Notice& notice)
    {
        if (mListApplied && !isExpected(notice.mID))
        {
            return false;
        }
        mHeld.push_back(notice);
        return true;
    }

    // The login's friends list has been applied: hands back what was held until now, in the order it came, but for
    // the notices about residents whose acceptance is unanswered. Holds nothing more about anyone else.
    std::vector<Notice> applyList()
    {
        mListApplied = true;
        return take([this](const Notice& notice) { return !isExpected(notice.mID); });
    }

    // The agent accepted id's friendship offer, and the server has yet to answer.
    void expect(const LLUUID& id)
    {
        if (!isExpected(id))
        {
            mExpected.push_back(id);
        }
    }

    // id became a friend before the list is applied: holds that in turn with the notices about them, the list being
    // made before it. Says whether it was held; once the list is applied it is not, and answered() hands back what
    // was held about them.
    bool formed(const LLUUID& id)
    {
        if (mListApplied)
        {
            return false;
        }
        stopExpecting(id);
        mHeld.push_back({ id, Kind::FORMED });
        return true;
    }

    // id became a friend, or the server refused or never answered the acceptance: hands back what was held about
    // them, in the order it came, and holds no more about them once the list is applied. Before then it hands back
    // nothing, and what was held about them comes back with the list, in turn with the rest.
    std::vector<Notice> answered(const LLUUID& id)
    {
        stopExpecting(id);
        if (!mListApplied)
        {
            return {};
        }
        return take([&id](const Notice& notice) { return notice.mID == id; });
    }

    bool isListApplied() const { return mListApplied; }
    bool isExpected(const LLUUID& id) const { return std::find(mExpected.begin(), mExpected.end(), id) != mExpected.end(); }
    size_t heldCount() const { return mHeld.size(); }

private:
    void stopExpecting(const LLUUID& id)
    {
        mExpected.erase(std::remove(mExpected.begin(), mExpected.end(), id), mExpected.end());
    }

    // The notices held that taken() picks, in the order they came, leaving the rest held.
    template <class Pred>
    std::vector<Notice> take(Pred taken)
    {
        std::vector<Notice> picked;
        std::vector<Notice> kept;
        for (const Notice& notice : mHeld)
        {
            (taken(notice) ? picked : kept).push_back(notice);
        }
        mHeld = std::move(kept);
        return picked;
    }

    std::vector<Notice> mHeld;
    // residents whose friendship offer the agent accepted, until the server answers
    std::vector<LLUUID> mExpected;
    bool                mListApplied = false;
};
