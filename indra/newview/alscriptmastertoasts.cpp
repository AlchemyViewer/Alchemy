/**
 * @file alscriptmastertoasts.cpp
 * @brief What came of sends from files on disk, said with no studio window open.
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

#include "alscriptmastertoasts.h"

#include "llcallbacklist.h"
#include "lldir.h"
#include "llnotificationsutil.h"
#include "lltrans.h"

namespace
{
    // How long what is heard is gathered before it is said; and how many are
    // said in full, the rest counted.
    constexpr F32    GATHER  = 2.f;
    constexpr size_t IN_FULL = 3;
}

// static
bool ALScriptMasterToasts::worthSaying(const Outcome& outcome)
{
    switch (outcome.what)
    {
        case Outcome::What::Sent: return outcome.result && !outcome.result->success;
        case Outcome::What::Skipped: return false;
        default: return true;
    }
}

void ALScriptMasterToasts::heard(const Outcome& outcome)
{
    if (!worthSaying(outcome))
    {
        return;
    }
    mGathered.push_back(outcome);
    if (mGathered.size() > 1)
    {
        return;
    }
    const std::weak_ptr<bool> alive = mAlive;
    doAfterInterval(
        [this, alive]() {
            if (alive.lock())
            {
                say();
            }
        },
        GATHER);
}

void ALScriptMasterToasts::say()
{
    std::vector<Outcome> gathered;
    gathered.swap(mGathered);
    if (gathered.empty())
    {
        return;
    }
    std::string message;
    for (size_t i = 0; i < gathered.size() && i < IN_FULL; ++i)
    {
        message += (i ? " " : "") + wordsFor(gathered[i]);
    }
    LLStringUtil::format_map_t args;
    if (gathered.size() > IN_FULL)
    {
        args["[COUNT]"] = llformat("%d", (S32)(gathered.size() - IN_FULL));
        message += " " + LLTrans::getString("ScriptMasterToastMore", args);
    }
    message += " " + LLTrans::getString("ScriptMasterToastOpen");
    LLSD said;
    said["MESSAGE"] = message;
    LLNotificationsUtil::add("ScriptStudioMasters", said);
}

// static
std::string ALScriptMasterToasts::wordsFor(const Outcome& outcome)
{
    // Of many scripts at once, and in words already.
    if (outcome.what == Outcome::What::NotSent)
    {
        return outcome.why;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = outcome.itemName;
    args["[FILE]"] = gDirUtilp->getBaseFileName(outcome.master);
    args["[WHY]"]  = outcome.why;
    const char* key = "ScriptMasterToastFailed";
    switch (outcome.what)
    {
        case Outcome::What::Sent: key = "ScriptMasterToastNotCompiled"; break;
        case Outcome::What::Failed: key = "ScriptMasterToastFailed"; break;
        case Outcome::What::Skipped: key = "ScriptMasterToastFailed"; break;
        case Outcome::What::Held: key = "ScriptMasterToastHeld"; break;
        case Outcome::What::Differing: key = "ScriptMasterToastDiffering"; break;
        case Outcome::What::Suspended: key = "ScriptMasterToastSuspended"; break;
        case Outcome::What::Orphaned: key = "ScriptMasterToastOrphaned"; break;
        case Outcome::What::Pending: key = "ScriptMasterToastPending"; break;
        case Outcome::What::NotSent: break;
    }
    return LLTrans::getString(key, args);
}
