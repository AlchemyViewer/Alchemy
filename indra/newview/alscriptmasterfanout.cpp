/**
 * @file alscriptmasterfanout.cpp
 * @brief The linked scripts an include's save sends again.
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

#include "alscriptmasterfanout.h"

#include "almasterplan.h"
#include "alscriptdiskmasters.h"
#include "alscriptmasterupload.h"
#include "lldir.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "llviewercontrol.h"

namespace
{
    // How many probes at a time: each reads its master and expands it.
    constexpr size_t PROBES = 4;
}

void ALScriptMasterFanOut::changed(const std::vector<std::string>& includes, const std::vector<std::string>& sent)
{
    if (includes.empty())
    {
        return;
    }
    mNextIncludes.insert(mNextIncludes.end(), includes.begin(), includes.end());
    mNextSent.insert(mNextSent.end(), sent.begin(), sent.end());
    if (!mBusy)
    {
        start();
    }
}

void ALScriptMasterFanOut::start()
{
    mIncludes.clear();
    mIncludes.swap(mNextIncludes);
    std::vector<std::string> sent;
    sent.swap(mNextSent);
    mToProbe.clear();
    mChanging.clear();
    if (mIncludes.empty())
    {
        mBusy = false;
        return;
    }
    // The users of every file saved, each once: not those sent already as a
    // save of their master, nor those a file saved is the master of.
    boost::unordered_flat_set<std::string> masters;
    for (const std::string& path : sent)
    {
        masters.insert(ALMasterLinks::keyOf(path));
    }
    for (const std::string& path : mIncludes)
    {
        masters.insert(ALMasterLinks::keyOf(path));
    }
    boost::unordered_flat_set<std::string> users;
    ALScriptDiskMasters&                   index = ALScriptDiskMasters::instance();
    for (const std::string& path : mIncludes)
    {
        for (const ALMasterLink& link : index.affectedBy(path))
        {
            if (!masters.contains(ALMasterLinks::keyOf(link.master)) && users.insert(ALScriptRef(link.object, link.item).id()).second)
            {
                mToProbe.push_back(link);
            }
        }
    }
    mBusy = true;
    if (mToProbe.empty())
    {
        start();
        return;
    }
    probeMore();
}

void ALScriptMasterFanOut::probeMore()
{
    // A probe may answer as it is asked, a script needing no expanding: the
    // next taken by the loop, not by a call inside it.
    if (mInProbeMore)
    {
        return;
    }
    mInProbeMore = true;
    while (mProbing < PROBES && !mToProbe.empty())
    {
        ALMasterLink link = std::move(mToProbe.front());
        mToProbe.pop_front();
        ++mProbing;
        // Whether it would change is all that is asked: the world is looked
        // at as it is sent.
        const std::weak_ptr<bool> alive = mAlive;
        ALScriptMasterUpload::probe(
            link,
            [this, alive, link](const ALScriptMasterUpload::Probe& found) {
                if (alive.lock())
                {
                    // One that could not go up is sent all the same, to say why.
                    probed(link, found.what != ALScriptDiskMasters::Outcome::What::Sent || !found.unchanged);
                }
            },
            /*world*/ false);
    }
    mInProbeMore = false;
    if (mProbing == 0 && mToProbe.empty())
    {
        decide();
    }
}

void ALScriptMasterFanOut::probed(const ALMasterLink& link, bool changing)
{
    --mProbing;
    if (changing)
    {
        mChanging.push_back(link);
    }
    probeMore();
}

void ALScriptMasterFanOut::decide()
{
    if (mChanging.empty())
    {
        start();
        return;
    }
    static LLCachedControl<U32> ask_over(gSavedSettings, "ALScriptMastersAskOver", 8);
    std::vector<LLUUID>         objects;
    for (const ALMasterLink& link : mChanging)
    {
        objects.push_back(link.object);
    }
    const size_t in_objects = ALMasterPlan::objectsOf(objects);
    if (!ALMasterPlan::askFirst(mChanging.size(), in_objects, ask_over))
    {
        sendAll(true);
        return;
    }
    // Asked once for them all, the files named; and in how many places, an
    // object or the inventory each.
    LLStringUtil::format_map_t places;
    places["[COUNT]"] = llformat("%d", (S32)in_objects);
    LLSD question;
    question["FILES"]  = filesNamed();
    question["COUNT"]  = (S32)mChanging.size();
    question["PLACES"] = LLTrans::getString(in_objects > 1 ? "ScriptMasterPlacesB" : "ScriptMasterPlacesA", places);
    const std::weak_ptr<bool> alive = mAlive;
    LLNotificationsUtil::add("ScriptStudioMastersSendUsers", question, LLSD(), [this, alive](const LLSD& notification, const LLSD& response) {
        if (alive.lock())
        {
            sendAll(LLNotificationsUtil::getSelectedOption(notification, response) == 0);
        }
    });
}

void ALScriptMasterFanOut::sendAll(bool send)
{
    ALScriptDiskMasters&     index = ALScriptDiskMasters::instance();
    std::vector<ALScriptRef> refs;
    for (const ALMasterLink& link : mChanging)
    {
        refs.emplace_back(link.object, link.item);
    }
    if (send)
    {
        for (const ALScriptRef& ref : refs)
        {
            index.send(ref, ALMasterPlan::Send::Derived);
        }
    }
    else
    {
        // Not sent, and waiting to be: Send from Files sends them. Said once
        // for them all, as the question was asked -- those linked still,
        // since the question waited on the scripter -- wherever the sends'
        // outcomes are said.
        if (const size_t pending = index.markPending(refs); pending > 0)
        {
            LLStringUtil::format_map_t args;
            args["[COUNT]"] = llformat("%d", (S32)pending);
            args["[FILES]"] = filesNamed();
            ALScriptDiskMasters::Outcome said;
            said.what = ALScriptDiskMasters::Outcome::What::NotSent;
            said.why  = LLTrans::getString(pending > 1 ? "ScriptMasterNotSentB" : "ScriptMasterNotSentA", args);
            index.tell(said);
        }
    }
    mChanging.clear();
    start();
}

std::string ALScriptMasterFanOut::filesNamed() const
{
    if (mIncludes.empty())
    {
        return std::string();
    }
    LLStringUtil::format_map_t files;
    files["[FILE]"]  = gDirUtilp->getBaseFileName(mIncludes.front());
    files["[COUNT]"] = llformat("%d", (S32)(mIncludes.size() - 1));
    return LLTrans::getString(mIncludes.size() > 2 ? "ScriptMasterFilesB" : mIncludes.size() > 1 ? "ScriptMasterFilesTwo" : "ScriptMasterFilesA", files);
}
