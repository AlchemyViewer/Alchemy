/**
 * @file alscriptsaveflow.h
 * @brief Where a save of a Script Studio tab stands, and where it goes next.
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

#include "alsourcemap.h"
#include "altextundo.h"
#include "lluuid.h"

#include <optional>
#include <vector>

// A save of one tab, from the moment it is asked for to the compiler's
// answer: which stage it is at, and what it is to do next as each thing it
// waits on answers. It says; the window does -- tidies, checks,
// preprocesses, sends, reports, closes -- and tells it what came of it.
// Nothing here draws or asks anything of the viewer, so that whole saves
// can be run through it in a test.
//
// A save is one of:
//   Idle           nothing on its way
//   Checking       waiting on the analyzers' check of the text as it stands
//   Preprocessing  waiting on a run of the preprocessor: its own, or one
//                  that was on its way already, which it joins
//   Sending        sent, waiting on the compiler's answer
// A save asked for while one is sent follows it once it answers. One that
// stops says why, and a close waiting on it stops with it.
class ALScriptSaveFlow
{
public:
    enum class Stage : U8
    {
        Idle,
        Checking,
        Preprocessing,
        Sending
    };
    Stage stage() const { return mStage; }
    bool  underway() const { return mStage != Stage::Idle; }
    bool  sending() const { return mStage == Stage::Sending; }

    // The checks that may stop a save. One that stopped a save is let past
    // -- it alone, for that text alone -- where the author asks again over
    // the same text: Save Anyway, or Save a second time. A later check
    // still stops it, and says why.
    enum Check : U8
    {
        CheckAnalyzers    = 1,
        CheckPreprocessor = 2,
        CheckPending      = 4,
        CheckAll          = 0xFF
    };

    // --- asking --------------------------------------------------------------------

    // A save asked for, of a tab whose item is out of reach -- its object
    // out of sight, the item gone, the connection lost, the script locked
    // or not loaded -- or a kept text detached from it, loaded under it
    // before anything is saved. Queued where one is being sent, which this
    // one follows once it answers.
    enum class Start : U8
    {
        Go,
        Queued,
        OutOfReach,
        Detached
    };
    Start ask(bool out_of_reach, bool detached);
    // Whether the safe fixes are to be made ahead of this save: once a save,
    // however many checks it waits on.
    bool fixOnce();

    // The tab as the save finds it once it is tidied.
    struct Tab
    {
        // The text's version.
        S64  version = 0;
        bool file     = false;
        bool notecard = false;
        // Whether the preprocessor runs over it, and whether a run of it
        // is on its way already.
        bool preprocessed     = false;
        bool preprocessorBusy = false;
        // Whether a save is held on the analyzers' errors (the setting);
        // whether their last check is of this text; and how many errors it
        // found.
        bool holdOnErrors  = false;
        bool checked       = false;
        S32  checkerErrors = 0;
    };
    // Where the save goes from here: a file written on the spot, a notecard
    // sent as it stands; the analyzers asked first, or their errors
    // stopping it; the preprocessor run, or joined; or the text sent.
    enum class Route : U8
    {
        File,
        Notecard,
        Check,
        StoppedByAnalyzers,
        Preprocess,
        JoinPreprocessor,
        Send
    };
    Route route(const Tab& tab);

    // --- what it waits on answering ---------------------------------------------

    // The analyzers' check answered: true where a save waited on it, and is
    // to be asked for again.
    bool checked();
    // A run of the preprocessor answered, of the text at `asked`, the text
    // now at `now`.
    struct Run
    {
        S64  asked   = 0;
        S64  now     = 0;
        bool errors  = false;
        bool pending = false;
    };
    // No save waited on it; the text moved on while it ran, and the save is
    // asked for again; its errors, or an include still to come, stopped the
    // save; or what it made is to be sent.
    enum class Landed : U8
    {
        NotForSave,
        MovedOn,
        StoppedByErrors,
        StoppedByPending,
        Send
    };
    Landed preprocessed(const Run& run);
    // Sent: the journal's save point as the text went, which the answer
    // marks saved whatever is typed meanwhile; the map the expansion went
    // through, where it was expanded; a notecard's items.
    void sent(const ALTextUndo::SavePoint& at, std::optional<ALSourceMap> map, std::vector<LLUUID> items);
    // It could not go, said why, or stopped where it was: nothing on its
    // way, the safe fixes to be made again next time, and no close waiting
    // on it any more.
    void stopped();
    // Done on the spot, with nothing to wait on: a file written.
    void done();
    // The compiler's answer: whether the text went up at all, whether it
    // compiled, and whether the viewer is quitting on this window.
    struct Answer
    {
        bool up       = false;
        bool compiled = false;
        bool quitting = false;
    };
    // Whether it answers this tab's save, rather than a recompile made from
    // elsewhere; whether the text is to be marked saved; and whether it
    // stopped the save -- the text did not go up, or it went up and did not
    // compile, unless the viewer is quitting and the tab was to close.
    struct Landing
    {
        bool ours      = false;
        bool markSaved = false;
        bool stopped   = false;
    };
    Landing compiled(const Answer& answer);
    // A save asked for while this one was on its way, taken to be made now.
    bool takeAgain();

    // --- the gate ------------------------------------------------------------------

    bool letsPast(S64 version, U8 checks) const { return mGateVersion == version && (mLetPast & checks) == checks; }
    // Asked again over the text the last save was stopped at: past what
    // stopped it.
    void letPast(S64 version);
    // Saved over whatever the checks would find: a copy made to be kept.
    void letAllPast(S64 version);

    // --- about it ------------------------------------------------------------------

    // The tab closes once the save lands.
    bool closeAfter() const { return mCloseAfter; }
    void setCloseAfter(bool close) { mCloseAfter = close; }
    // The save came from an external editor, which is not written back to,
    // over whatever the checks would find.
    void fromExternal(S64 version);
    bool external() const { return mExternal; }
    void endExternal() { mExternal = false; }
    // What went up with the last save.
    const ALTextUndo::SavePoint&      savePoint() const { return mSavePoint; }
    const std::optional<ALSourceMap>& sentMap() const { return mSentMap; }
    const std::vector<LLUUID>&        sentItems() const { return mSentItems; }
    void                              forgetSentItems() { mSentItems.clear(); }
    // The text a save sent, whose weight is said once it is known, where it
    // is over the target's limit; -1 for none.
    S64  warnWeightFor() const { return mWarnWeightFor; }
    void setWarnWeightFor(S64 version) { mWarnWeightFor = version; }

private:
    void stoppedBy(S64 version, U8 check);

    Stage                      mStage      = Stage::Idle;
    bool                       mAgain      = false;
    bool                       mCloseAfter = false;
    bool                       mExternal   = false;
    bool                       mFixed      = false;
    // The text the checks let past are for, which checks those are, and
    // which stopped the last save of it.
    S64                        mGateVersion = -1;
    U8                         mLetPast     = 0;
    U8                         mStoppedBy   = 0;
    ALTextUndo::SavePoint      mSavePoint;
    std::optional<ALSourceMap> mSentMap;
    std::vector<LLUUID>        mSentItems;
    S64                        mWarnWeightFor = -1;
};
