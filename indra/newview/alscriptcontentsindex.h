/**
 * @file alscriptcontentsindex.h
 * @brief What each prim holds, as its region last said: one index for every Script Studio window, lookup and search.
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

#include "alscriptworkspace.h"
#include "lluuid.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// What each prim holds -- its scripts and notecards -- whether each script
// runs, and what the prim said it is called, as its region last said: one
// index, owned by the workspace, that every studio window's explorer, the
// lookups, Rename and the searches read. A prim is asked once however many
// want it, and every one hears the answer by the signal. An operation over
// a whole object asks for every prim of it first (ensureListed), a large
// linkset's folded ones among them, and is told which could not be listed.
// Nothing of the world: which prims an object has, whether an object's copy
// of what a prim holds is current, and the asking, are the World's, which
// the workspace gives it and a test fakes.
class ALScriptContentsIndex
{
public:
    typedef ALScriptWorkspace::Item     Item;
    typedef ALScriptWorkspace::Contents Contents;

    struct World
    {
        // An object in sight by any prim of it: its root first, then its
        // other prims; nothing where it is not in sight, or is an avatar.
        std::function<std::vector<LLUUID>(const LLUUID& id)> linkset;
        // Whether the object's copy of what a prim holds is current: in
        // sight, and not changed since it was last asked.
        std::function<bool(const LLUUID& prim)> current;
        // A prim asked what it holds, of the region itself where
        // `from_region`; `told` once, answered or not.
        std::function<void(const LLUUID& prim, bool from_region, std::function<void(const Contents&)> told)> ask;
        // A script asked whether it runs; the answer comes to running().
        std::function<void(const ALScriptRef& ref)> askRunning;
    };

    explicit ALScriptContentsIndex(World world);

    // --- what each prim holds ----------------------------------------------------------

    struct Prim
    {
        // Answered by the region; until then, or where it never answered,
        // what it holds is not known.
        bool              fetched = false;
        std::vector<Item> items;
        // What its answer said it is called; empty where none said.
        std::string       name;
    };
    // What is known of a prim, or nothing where it was never answered.
    const Prim* prim(const LLUUID& id) const;
    // What it holds as far as known: nothing where it is not.
    const std::vector<Item>& items(const LLUUID& prim) const;
    bool                     fetched(const LLUUID& prim) const;

    // A prim asked what it holds: where it is not known, where the world's
    // copy of it is not current, or every time where `refetch`, and of the
    // region where `from_region`; not while it is asked and not answered,
    // unless `refetch`. True where it was asked now.
    bool ask(const LLUUID& prim, bool refetch = false, bool from_region = false);
    // Asked and not answered yet.
    bool asking(const LLUUID& prim) const { return mAsking.contains(prim); }

    // Every answer, with what it said: heard after the index took it in.
    typedef boost::signals2::signal<void(const Contents&)> heard_signal_t;
    boost::signals2::connection onHeard(const heard_signal_t::slot_type& slot) { return mHeard.connect(slot); }

    // --- a whole object ------------------------------------------------------------------

    // Every prim of an object listed, for an operation over all of it: its
    // prims, the root first, and those whose contents could not be had --
    // the region did not answer -- which the operation says it passed over.
    // An object not in sight has no prims, and is not `present`.
    struct Listed
    {
        LLUUID              root;
        bool                present = false;
        std::vector<LLUUID> prims;
        std::vector<LLUUID> unlisted;
    };
    // Each prim of the object by any prim of it asked what it holds where
    // that is not known and current, and `done` told once every one has
    // answered; at once where none needed asking.
    void ensureListed(const LLUUID& id, std::function<void(const Listed&)> done);

    // --- whether scripts run -------------------------------------------------------------

    // As the region last said; nothing where it has not.
    std::optional<bool> running(const ALScriptRef& ref) const;
    void                running(const ALScriptRef& ref, bool running) { mRunning[{ ref.object, ref.item }] = running; }
    // Every script's that is known, by prim and item.
    const std::map<std::pair<LLUUID, LLUUID>, bool>& runningKnown() const { return mRunning; }
    // What was known of these prims' scripts let go of, for them to be asked
    // again.
    void forgetRunning(const std::vector<LLUUID>& prims);

    // --- letting go ----------------------------------------------------------------------

    // All known of a prim let go of: its object left, or was taken apart.
    void forget(const LLUUID& prim);

private:
    void heard(const Contents& contents);
    // The waits of ensureListed() that this answer ends, told.
    void settle();

    struct Wait
    {
        Listed                             listed;
        boost::unordered_flat_set<LLUUID>  left;
        std::function<void(const Listed&)> done;
        // Every prim asked: until then an answer on the spot settles
        // nothing, the rest not asked yet.
        bool                               armed = false;
    };

    World                                     mWorld;
    boost::unordered_flat_map<LLUUID, Prim>   mPrims;
    boost::unordered_flat_set<LLUUID>         mAsking;
    std::map<std::pair<LLUUID, LLUUID>, bool> mRunning;
    std::vector<std::shared_ptr<Wait>>        mWaits;
    heard_signal_t                            mHeard;
    // Let go of as the index is, so that an answer after it does nothing.
    std::shared_ptr<bool>                     mAlive = std::make_shared<bool>(true);
};
