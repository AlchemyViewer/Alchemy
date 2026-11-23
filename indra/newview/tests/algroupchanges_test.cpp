/**
 * @file algroupchanges_test.cpp
 * @brief Unit tests for telling the observers of each changed group of the change
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

#include <catch2/catch_test_macros.hpp>

#include "../algroupchanges.h"
#include "../llgroupmgr.h"

#include <map>
#include <set>
#include <vector>

namespace
{
    struct Group
    {
        bool mChanged = false;
    };

    // What every observer was told, in order: the group and the change.
    using Told = std::vector<std::pair<LLUUID, LLGroupChange>>;

    struct Observer : public LLGroupMgrObserver
    {
        Observer(const LLUUID& id, Told& told) : LLGroupMgrObserver(id), mTold(told) {}
        void changed(LLGroupChange gc) override { mTold.emplace_back(mID, gc); }
        Told& mTold;
    };

    struct Particular : public LLParticularGroupObserver
    {
        explicit Particular(Told& told) : mTold(told) {}
        void changed(const LLUUID& group_id, LLGroupChange gc) override { mTold.emplace_back(group_id, gc); }
        Told& mTold;
    };

    // In the order the manager's map of groups holds them.
    const LLUUID FIRST("00000000-0000-0000-0000-000000000001");
    const LLUUID SECOND("00000000-0000-0000-0000-000000000002");
    const LLUUID THIRD("00000000-0000-0000-0000-000000000003");

    using Groups = std::map<LLUUID, Group*>;
    using Observers = std::multimap<LLUUID, LLGroupMgrObserver*>;
    using ParticularObservers = std::map<LLUUID, std::set<LLParticularGroupObserver*>>;

    // A group the panels watch through the manager's observers alone, before one the group list watches through a
    // particular observer: the second is told too.
    TEST_CASE("algroupchanges: a changed group with no particular observers leaves the groups after it told", "[algroupchanges]")
    {
        Group first, second, third;
        first.mChanged = second.mChanged = third.mChanged = true;
        Groups groups{ { FIRST, &first }, { SECOND, &second }, { THIRD, &third } };

        Told told_manager, told_particular;
        Observer watching_first(FIRST, told_manager);
        Particular watching_second(told_particular), watching_third(told_particular);
        const Observers observers{ { FIRST, &watching_first } };
        const ParticularObservers particular{ { SECOND, { &watching_second } }, { THIRD, { &watching_third } } };

        ALGroupChanges::notify(groups, observers, particular, GC_MEMBER_DATA);

        CHECK(told_manager == Told{ { FIRST, GC_MEMBER_DATA } });
        CHECK(told_particular == Told{ { SECOND, GC_MEMBER_DATA }, { THIRD, GC_MEMBER_DATA } });
        CHECK_FALSE(first.mChanged);
        CHECK_FALSE(second.mChanged);
        CHECK_FALSE(third.mChanged);
    }

    TEST_CASE("algroupchanges: a group not changed is not told and stays as it was", "[algroupchanges]")
    {
        Group first, second;
        second.mChanged = true;
        Groups groups{ { FIRST, &first }, { SECOND, &second } };

        Told told;
        Observer watching_first(FIRST, told), watching_second(SECOND, told);
        Particular particular_first(told);
        const Observers observers{ { FIRST, &watching_first }, { SECOND, &watching_second } };
        const ParticularObservers particular{ { FIRST, { &particular_first } } };

        ALGroupChanges::notify(groups, observers, particular, GC_TITLES);

        CHECK(told == Told{ { SECOND, GC_TITLES } });
        CHECK_FALSE(first.mChanged);
        CHECK_FALSE(second.mChanged);
    }

    // An observer may stop observing while it is told, as a panel closing on a change does: the copy keeps the
    // others told.
    TEST_CASE("algroupchanges: an observer that stops observing while told leaves the others told", "[algroupchanges]")
    {
        Group first;
        first.mChanged = true;
        Groups groups{ { FIRST, &first } };

        Told told;
        Observers observers;
        struct Leaving : public LLGroupMgrObserver
        {
            Leaving(const LLUUID& id, Observers& observers, Told& told) : LLGroupMgrObserver(id), mObservers(observers), mTold(told) {}
            void changed(LLGroupChange gc) override
            {
                mTold.emplace_back(mID, gc);
                mObservers.clear();
            }
            Observers& mObservers;
            Told& mTold;
        };
        Leaving leaving(FIRST, observers, told);
        Observer staying(FIRST, told);
        observers.emplace(FIRST, &leaving);
        observers.emplace(FIRST, &staying);

        ALGroupChanges::notify(groups, observers, ParticularObservers{}, GC_ROLE_DATA);

        CHECK(told == Told{ { FIRST, GC_ROLE_DATA }, { FIRST, GC_ROLE_DATA } });
        CHECK(observers.empty());
    }
}
