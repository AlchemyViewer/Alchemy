/**
 * @file algroupchanges.h
 * @brief ALGroupChanges: telling the observers of each changed group of the change
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

// LLGroupMgr's notification of a change: every group marked changed is marked unchanged, and its observers told,
// first those keyed to it in the manager's multimap of observers, then those it holds a set of particular observers
// for. A group that has observers of one kind and not the other is told through the kind it has, and the groups after
// it are told all the same. Each group's observers are copied before they are told, since an observer may stop
// observing while it is told.
struct ALGroupChanges
{
    // groups maps a group's ID to its data, which has mChanged; observers is a multimap from a group's ID to its
    // observers, and particular a map from a group's ID to a set of them.
    template <class Groups, class Observers, class ParticularObservers, class Change>
    static void notify(Groups& groups, const Observers& observers, const ParticularObservers& particular, Change gc)
    {
        for (auto gi = groups.begin(); gi != groups.end(); ++gi)
        {
            if (!gi->second->mChanged)
            {
                continue;
            }
            const LLUUID group_id = gi->first;

            const Observers observers_now = observers;
            for (auto oi = observers_now.lower_bound(group_id), end = observers_now.upper_bound(group_id); oi != end; ++oi)
            {
                oi->second->changed(gc);
            }
            gi->second->mChanged = false;

            const auto particular_it = particular.find(group_id);
            if (particular_it == particular.end())
            {
                continue;
            }
            const auto particular_now = particular_it->second;
            for (auto* observer : particular_now)
            {
                observer->changed(group_id, gc);
            }
        }
    }
};
