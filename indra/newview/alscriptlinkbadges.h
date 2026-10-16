/**
 * @file alscriptlinkbadges.h
 * @brief How each in-world script linked to a file on disk stands, for the studio's windows to mark it by.
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

#include "almasterlinks.h"
#include "alscripttypes.h"
#include "alwatchedfile.h"
#include "llsingleton.h"
#include "llstl.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALSerialWorker;

// How each script linked to a file on disk stands (ALScriptDiskMasters),
// and each notecard, for the studio's windows to mark it by -- the
// Explorer's rows, a master file's tab -- without asking anything of the
// disk as they draw: made
// from the links as they are, and from the masters' stamps as they were
// last looked at. The stamps are looked at off the main thread, whenever
// the links change and whenever an Explorer lists again, and never as a
// row is drawn. One for every window, so that the files are looked at once
// however many windows are open.
class ALScriptLinkBadges : public LLSingleton<ALScriptLinkBadges>
{
    LLSINGLETON(ALScriptLinkBadges);
    ~ALScriptLinkBadges() override;
    void initSingleton() override;
    void cleanupSingleton() override;

public:
    // How a linked script stands. Where more than one holds, the one
    // furthest down is said: a master gone matters more than a change in
    // the world, and that more than a send waiting or a file changed.
    enum class Badge : U8
    {
        // Linked, and nothing known to be wrong.
        Linked,
        // Never sent from its file since it was linked: what the world
        // holds may not be what the file makes.
        Unsent,
        // The file changed on disk since it was last sent.
        Newer,
        // A send that did not go, waiting to be sent by hand.
        Pending,
        // Changed in the world by something else since the last send.
        Differing,
        // Its master moved or went: held, or not found as last looked at.
        Suspended
    };
    struct Mark
    {
        Badge       badge = Badge::Linked;
        std::string master;
        bool        operator==(const Mark&) const = default;
    };

    // A script's mark; none for one linked to no file, or whose item has
    // left the world.
    std::optional<Mark> markOf(const ALScriptRef& ref) const;
    // How many of a prim's scripts are linked to files.
    S32 linkedIn(const LLUUID& prim) const;
    // How many scripts a file on disk is the master of, by its path as a
    // tab holds it: compared as the links compare paths, nothing asked of
    // the disk.
    S32 masteredBy(const std::string& path) const;

    // The links read again, and the masters' stamps looked at again off
    // the main thread: one look at a time, and one more after it however
    // many are asked for while it is under way.
    void refresh();

    // The marks, or how many scripts a file masters, changed.
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

private:
    typedef boost::unordered_flat_map<std::string, ALFileStamp, ll::string_hash, std::equal_to<>> Stamps;

    // The marks and the counts made again from the links and the stamps
    // in hand; said where they changed.
    void remark();
    void look();
    void looked(Stamps stamps);

    std::vector<ALMasterLink>                                                    mLinks;
    Stamps                                                                       mStamps;
    boost::unordered_flat_map<ALScriptRef, Mark>                                 mMarks;
    boost::unordered_flat_map<LLUUID, S32>                                       mByPrim;
    boost::unordered_flat_map<std::string, S32, ll::string_hash, std::equal_to<>> mByMaster;
    changed_signal_t                                                             mChanged;
    boost::signals2::scoped_connection                                           mLinksConnection;
    // The thread the stamps are looked at on, made with the first look.
    std::unique_ptr<ALSerialWorker>                                              mWorker;
    bool                                                                         mLooking   = false;
    bool                                                                         mLookAgain = false;
    // Held while this is, for a look's answer to know it still is.
    std::shared_ptr<bool>                                                        mAlive = std::make_shared<bool>(true);
};
