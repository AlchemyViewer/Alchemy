/**
 * @file alscriptdiskmasters.h
 * @brief The account's in-world scripts whose master is a file on disk.
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

#include "aldiskincludes.h"
#include "almasterlinks.h"
#include "almasterplan.h"
#include "alscripttypes.h"
#include "llsingleton.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <optional>
#include <string>
#include <vector>

// The scripts in the world, the agent's own inventory's and its objects',
// whose master is a file on disk: the file is what the script is, and
// saving it sends it up. Which file masters which script is the account's
// (ALMasterLinks, in script_masters.llsd under the account's folder), so
// that another account on this computer never uploads into this one's
// objects; a link is made only by a person's click, and nothing a script
// says makes one. Each send is ALScriptMasterUpload's: the file read and
// expanded as a file on disk is, the world asked what it holds first, and
// what the plan says done (ALMasterPlan) -- a save of the master always
// goes up, keeping first what the world had where it moved. What came of
// each send is told to whoever listens: every studio window's Output.
class ALScriptDiskMasters : public LLSingleton<ALScriptDiskMasters>
{
    LLSINGLETON(ALScriptDiskMasters);
    ~ALScriptDiskMasters() override;

public:
    // What came of a send, or of the world heard changing under a link.
    struct Outcome
    {
        enum class What : U8
        {
            // Sent and compiled, or not compiled: the answer in `result`.
            Sent,
            // Not sent: the upload failed, or the master could not be read.
            Failed,
            // Not sent, nothing to send: what is in the world already.
            Skipped,
            // Not sent: changed in the world since the last send from the
            // master, and a send of the studio's own does not go over it.
            Held,
            // Changed in the world by something else, heard as it was.
            Differing,
            // Its master gone from disk, or no longer one a hint may reach.
            Suspended,
            // Its item gone from its object or the inventory.
            Orphaned,
            // Its object out of reach: sent when a person asks again.
            Pending
        };
        What        what = What::Sent;
        ALScriptRef ref;
        std::string master;
        std::string itemName;
        // A send of the master itself, or the studio's own.
        bool        direct = true;
        // The world's text kept in History before it was gone over.
        bool        keptTheirs = false;
        // Who changed it in the world, where it was heard (Differing).
        ALScriptOrigin by = ALScriptOrigin::Studio;
        std::optional<ALScriptCompileResult> result;
        // What the preprocessor found, which the script went up with.
        std::vector<ALScriptDiagnostic> preprocessed;
        std::string why;
    };
    typedef boost::signals2::signal<void(const Outcome&)> outcome_signal_t;
    boost::signals2::connection onOutcome(const outcome_signal_t::slot_type& slot) { return mOutcome.connect(slot); }
    // The links changed: made, let go of, or come to stand otherwise.
    typedef boost::signals2::signal<void()> changed_signal_t;
    boost::signals2::connection onChanged(const changed_signal_t::slot_type& slot) { return mChanged.connect(slot); }

    // The link of a script, and every script a file masters; nothing before
    // the account is known.
    std::optional<ALMasterLink> linkOf(const ALScriptRef& ref);
    std::vector<ALMasterLink>   mastering(const std::string& master);
    bool                        masters(const std::string& master) { return !mastering(master).empty(); }
    // Every link, and those of the items in one object -- a null one the
    // agent's own inventory -- in the order they were made.
    std::vector<ALMasterLink> all();
    std::vector<ALMasterLink> linksIn(const LLUUID& object);
    // A link made, or one replaced; and one let go of. Written out at once.
    void link(ALMasterLink link);
    void unlink(const ALScriptRef& ref);

    // A master written by the studio: each script it masters sent, as a
    // save of the master is. Nothing for a file that masters nothing.
    void wrote(const std::string& path);
    // A script sent from its master now: as a save of it is, or as the
    // studio's own send is, which a change in the world holds.
    void send(const ALScriptRef& ref, ALMasterPlan::Send kind);

    // What a send found, the link as it stands after it: called by
    // ALScriptMasterUpload as each one ends.
    void finished(const Outcome& outcome, const std::optional<ALMasterLink>& updated);

    // The folders a script on disk may read from, as its includes and
    // requires do -- the include folders, what a configuration on disk
    // blesses, the aliases' folders on disk -- which a hint may reach and
    // nothing else; and the aliases by name.
    static ALDiskIncludes                                   blessedFor(const std::string& master, bool lua);
    static std::vector<std::pair<std::string, std::string>> aliasesFor(const std::string& master, bool lua);
    // What an upload header's @file names a master by: its path from the
    // blessed folder it is under; nothing where it is under none.
    static std::string fileLabel(const std::string& master, const ALDiskIncludes& blessed);
    // The item a link names, as the world has it now; null where it is out
    // of reach or gone.
    static LLInventoryItem* itemOf(const ALScriptRef& ref);

private:
    // The links of the account in hand: loaded the first time they are
    // asked for, and again for another account.
    ALMasterLinks* links();
    void           save();
    void           changed();
    // A save of a linked script heard from elsewhere.
    void heardSaved(const ALScriptSaved& saved);

    std::string                        mFor;
    ALMasterLinks                      mLinks;
    boost::signals2::scoped_connection mSavedConnection;
    outcome_signal_t                   mOutcome;
    changed_signal_t                   mChanged;
    // The scripts a send is on its way to, by their id, and whether another
    // is asked for after it, of the kind asked last: one at a time each,
    // the newest text going up last.
    boost::unordered_flat_map<std::string, std::optional<ALMasterPlan::Send>> mSending;
};
