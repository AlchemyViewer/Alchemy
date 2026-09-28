/**
 * @file alscriptsaveflow.cpp
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


#include "llviewerprecompiledheaders.h"

#include "alscriptsaveflow.h"

#include <utility>

ALScriptSaveFlow::Start ALScriptSaveFlow::ask(bool out_of_reach, bool detached)
{
    if (mStage == Stage::Sending)
    {
        // One on its way: this one goes when it answers, with whatever is
        // unsaved by then -- an editor outside saving again while the last
        // compiles, a key pressed twice.
        mAgain = true;
        return Start::Queued;
    }
    // Nothing is tried that would only fail.
    if (out_of_reach)
    {
        stopped();
        return Start::OutOfReach;
    }
    if (detached)
    {
        stopped();
        return Start::Detached;
    }
    return Start::Go;
}

bool ALScriptSaveFlow::fixOnce()
{
    return !std::exchange(mFixed, true);
}

ALScriptSaveFlow::Route ALScriptSaveFlow::route(const Tab& tab)
{
    // A decision afresh: whatever the save waited on before is what it is
    // asked again for having come.
    mStage = Stage::Idle;
    if (tab.file)
    {
        return Route::File;
    }
    if (tab.notecard)
    {
        return Route::Notecard;
    }
    // The analyzers' errors hold it where the scripter asked for that,
    // unless let past for this text: all of them, for a copy made to be
    // kept or an external editor's save; or theirs, asked again after
    // they stopped it.
    if (tab.holdOnErrors && !letsPast(tab.version, CheckAnalyzers))
    {
        if (!tab.checked)
        {
            mStage = Stage::Checking;
            return Route::Check;
        }
        if (tab.checkerErrors > 0)
        {
            stoppedBy(tab.version, CheckAnalyzers);
            stopped();
            return Route::StoppedByAnalyzers;
        }
    }
    if (tab.preprocessed)
    {
        // Expanded first, with its includes fetched; the upload follows. A
        // run on its way already -- the one a load starts, which fetches an
        // object's includes and can take a while -- is waited on rather
        // than started again.
        mStage = Stage::Preprocessing;
        return tab.preprocessorBusy ? Route::JoinPreprocessor : Route::Preprocess;
    }
    return Route::Send;
}

bool ALScriptSaveFlow::checked()
{
    if (mStage != Stage::Checking)
    {
        return false;
    }
    mStage = Stage::Idle;
    return true;
}

ALScriptSaveFlow::Landed ALScriptSaveFlow::preprocessed(const Run& run)
{
    if (mStage != Stage::Preprocessing)
    {
        return Landed::NotForSave;
    }
    mStage = Stage::Idle;
    if (run.now != run.asked)
    {
        // The text moved on while the includes came: saved again from the
        // start.
        return Landed::MovedOn;
    }
    // Sent whatever it found -- errors, an include that never came: what
    // it goes up with, or without, is the window's to say as it goes.
    return Landed::Send;
}

void ALScriptSaveFlow::sent(const ALTextUndo::SavePoint& at, std::optional<ALSourceMap> map, std::vector<LLUUID> items, U64 request)
{
    mRequest     = request;
    mStage       = Stage::Sending;
    mFixed       = false;
    mGateVersion = -1;
    mLetPast     = 0;
    mStoppedBy   = 0;
    mSavePoint   = at;
    // What the compiler's lines are read back through, kept as it went:
    // the next run of the preprocessor, for whatever reason, is of another
    // text.
    mSentMap   = std::move(map);
    mSentItems = std::move(items);
}

void ALScriptSaveFlow::stopped()
{
    mStage      = Stage::Idle;
    mCloseAfter = false;
    mFixed      = false;
}

void ALScriptSaveFlow::done()
{
    mStage = Stage::Idle;
    mFixed = false;
}

ALScriptSaveFlow::Landing ALScriptSaveFlow::compiled(const Answer& answer)
{
    Landing landing;
    landing.ours = mStage == Stage::Sending && answer.request == mRequest;
    if (landing.ours)
    {
        mStage = Stage::Idle;
    }
    if (!answer.up)
    {
        // What was asked for meanwhile would meet the same; Retry is
        // offered.
        mAgain = false;
        if (landing.ours)
        {
            stopped();
            landing.stopped = true;
        }
        return landing;
    }
    // The text is the server's now, compiled or not -- as it was sent,
    // whatever was typed while the answer came.
    landing.markSaved = landing.ours;
    // Saved, but not running: a close waiting on it leaves the tab open
    // with what the compiler said, rather than taking both away -- but for
    // the viewer quitting, when the text is saved and that was what was
    // asked.
    if (!answer.compiled && landing.ours && !(mCloseAfter && answer.quitting))
    {
        stopped();
        landing.stopped = true;
    }
    return landing;
}

bool ALScriptSaveFlow::takeAgain()
{
    return std::exchange(mAgain, false);
}

void ALScriptSaveFlow::letPast(S64 version)
{
    if (mGateVersion == version)
    {
        mLetPast |= mStoppedBy;
    }
}

void ALScriptSaveFlow::letAllPast(S64 version)
{
    mGateVersion = version;
    mLetPast     = CheckAll;
    mStoppedBy   = 0;
}

void ALScriptSaveFlow::fromExternal(S64 version)
{
    mExternal = true;
    letAllPast(version);
}

void ALScriptSaveFlow::stoppedBy(S64 version, U8 check)
{
    if (mGateVersion != version)
    {
        mGateVersion = version;
        mLetPast     = 0;
    }
    mStoppedBy = check;
}
