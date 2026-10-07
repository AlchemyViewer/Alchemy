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
#include "almasterqueue.h"
#include "alscripttypes.h"
#include "llsingleton.h"

#include <boost/signals2.hpp>
#include <boost/unordered/unordered_flat_map.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

class ALScriptMasterFanOut;
class ALScriptMasterToasts;
class ALScriptMasterWatch;

// The scripts in the world, the agent's own inventory's and its objects',
// whose master is a file on disk: the file is what the script is, and
// saving it sends it up. Which file masters which script is the account's
// (ALMasterLinks, in script_masters.llsd under the account's folder), so
// that another account on this computer never uploads into this one's
// objects; a link is made only by a person's click, and nothing a script
// says makes one. Each send is ALScriptMasterUpload's: the file read and
// expanded as a file on disk is, the world asked what it holds first, and
// what the plan says done (ALMasterPlan) -- a save of the master always
// goes up, keeping first what the world had where it moved. Four go at a
// time, and one at a time for each script (ALMasterQueue).
//
// Links are live while the agent is logged in, whether or not a studio
// window is open: the masters and the files they include are watched for
// saves made outside the studio (ALScriptMasterWatch, while
// `ALScriptMastersEnabled`), a master's save sending its scripts and an
// include's sending again those whose last expansion read it
// (ALScriptMasterFanOut). What came of each send is told to whoever
// listens -- every studio window's Output, kept for the window opened
// next where there is none -- and, with no window in sight, the failures
// and conflicts said in a toast (ALScriptMasterToasts).
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

    // The links read and watched, once the agent is in the world; again for
    // another account.
    void start();

    // The link of a script, and every script a file masters; nothing before
    // the account is known.
    std::optional<ALMasterLink> linkOf(const ALScriptRef& ref);
    std::vector<ALMasterLink>   mastering(const std::string& master);
    bool                        masters(const std::string& master) { return !mastering(master).empty(); }
    // Every link, and those of the items in one object -- a null one the
    // agent's own inventory -- in the order they were made.
    std::vector<ALMasterLink> all();
    std::vector<ALMasterLink> linksIn(const LLUUID& object);
    // The links a file's save may send again: those whose last expansion
    // read it, or missed an include (ALMasterLinks::affectedBy).
    std::vector<ALMasterLink> affectedBy(const std::string& include);
    // A link made, or one replaced; and one let go of. Written out at once.
    void link(ALMasterLink link);
    void unlink(const ALScriptRef& ref);
    // Scripts not sent when they might have been, waiting to be sent by
    // hand.
    void markPending(const std::vector<ALScriptRef>& refs);

    // A file written by the studio: no outside save for the watch, each
    // script it masters sent as a save of the master is, and the scripts
    // that include it sent again.
    void wrote(const std::string& path);
    // A script sent from its master now: as a save of it is, or as the
    // studio's own send is, which a change in the world holds.
    void send(const ALScriptRef& ref, ALMasterPlan::Send kind);

    // What a send found, the link as it stands after it: called by
    // ALScriptMasterUpload as each one ends.
    void finished(const Outcome& outcome, const std::optional<ALMasterLink>& updated);
    // What was said with no studio window to hear it, for the window opened
    // next to list; given once.
    std::vector<Outcome> takeUnheard();

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
    // What is watched, as the links stand now.
    void           rewatch();
    // A save of a linked script heard from elsewhere.
    void heardSaved(const ALScriptSaved& saved);
    // Saves heard by the watch, once their burst went quiet.
    void released(const std::vector<std::string>& masters, const std::vector<std::string>& includes);
    // An outcome told: to the windows listening, or, with none, to the
    // toasts and kept.
    void tell(const Outcome& outcome);
    // Whether a studio window is open where it can be seen.
    static bool studioInSight();
    // The sends whose turn it is, started; and a script forgotten once
    // nothing is under way or waiting for it.
    void startTurns(std::vector<std::pair<std::string, ALMasterPlan::Send>> turns);
    void forgetQueued(const std::string& id);

    std::string                        mFor;
    ALMasterLinks                      mLinks;
    boost::signals2::scoped_connection mSavedConnection;
    boost::signals2::scoped_connection mEnabledConnection;
    outcome_signal_t                   mOutcome;
    changed_signal_t                   mChanged;
    // The sends under way and waiting, by the script's id, and the scripts
    // so named.
    ALMasterQueue                                                             mQueue;
    boost::unordered_flat_map<std::string, ALScriptRef, ll::string_hash, std::equal_to<>> mQueued;
    std::unique_ptr<ALScriptMasterWatch>                                      mWatch;
    std::unique_ptr<ALScriptMasterFanOut>                                     mFanOut;
    std::unique_ptr<ALScriptMasterToasts>                                     mToasts;
    std::vector<Outcome>                                                      mUnheard;
};
