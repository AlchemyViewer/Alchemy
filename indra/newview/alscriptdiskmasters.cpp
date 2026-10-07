/**
 * @file alscriptdiskmasters.cpp
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

#include "llviewerprecompiledheaders.h"

#include "alscriptdiskmasters.h"

#include "alfilewrite.h"
#include "allinelabel.h"
#include "alluauconfig.h"
#include "alscriptenvelope.h"
#include "alscriptmasterupload.h"
#include "alscriptmodules.h"
#include "alscriptpreprocessor.h"
#include "alscriptworkspace.h"
#include "aluploadheader.h"
#include "lldir.h"
#include "llinventorymodel.h"
#include "llsdserialize.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"

#include <sstream>

namespace
{
    constexpr const char* INDEX_FILE = "script_masters.llsd";

    // A path as the links keep it: the file's own, links followed.
    std::string canonical(const std::string& path)
    {
        const std::string identity = ALScriptModules::identity("disk:" + path);
        return identity.rfind("disk:", 0) == 0 ? identity.substr(5) : path;
    }

    // What the world's text would hash to beside ours: an envelope's halves,
    // or the text where it is none.
    std::string hashOfWorld(const std::string& text, const std::string& target)
    {
        if (const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(text))
        {
            return ALUploadHeader::hashOf(envelope->compileTarget.empty() ? target : envelope->compileTarget, envelope->source,
                                          envelope->expanded);
        }
        return ALUploadHeader::hashOfPlain(target, text);
    }
}

ALScriptDiskMasters::ALScriptDiskMasters()
{
    mSavedConnection = ALScriptWorkspace::instance().onSaved([this](const ALScriptSaved& saved) { heardSaved(saved); });
}

ALScriptDiskMasters::~ALScriptDiskMasters() = default;

ALMasterLinks* ALScriptDiskMasters::links()
{
    if (!gDirUtilp || gDirUtilp->getLindenUserDir().empty())
    {
        return nullptr;
    }
    const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_PER_SL_ACCOUNT, INDEX_FILE);
    if (path != mFor)
    {
        // Another account's, or the first asked for: read whole. What will
        // not read is no links, said, and not written over until a link
        // changes.
        mFor   = path;
        mLinks = ALMasterLinks();
        std::string text;
        if (ALFileRead::whole(path, text, ALDiskIncludes::MAX_BYTES))
        {
            LLSD               llsd;
            std::istringstream in(text);
            if (LLSDSerialize::fromXML(llsd, in) > 0)
            {
                mLinks = ALMasterLinks::fromLLSD(llsd);
            }
            else
            {
                LL_WARNS("ScriptMasters") << "Could not read " << path << "; no scripts are linked to files" << LL_ENDL;
            }
        }
        // Orphans kept a while, for a script taken and rezzed again.
        if (mLinks.prune(LLDate::now()) > 0)
        {
            save();
        }
    }
    return &mLinks;
}

void ALScriptDiskMasters::save()
{
    if (mFor.empty())
    {
        return;
    }
    std::ostringstream out;
    LLSDSerialize::toPrettyXML(mLinks.toLLSD(), out);
    if (!ALFileWrite::whole(mFor, out.str(), /*durable*/ true))
    {
        LL_WARNS("ScriptMasters") << "Could not write " << mFor << LL_ENDL;
    }
}

void ALScriptDiskMasters::changed()
{
    save();
    mChanged();
}

std::optional<ALMasterLink> ALScriptDiskMasters::linkOf(const ALScriptRef& ref)
{
    ALMasterLinks* all = links();
    const ALMasterLink* found = all ? all->of(ref.object, ref.item) : nullptr;
    return found ? std::optional<ALMasterLink>(*found) : std::nullopt;
}

std::vector<ALMasterLink> ALScriptDiskMasters::mastering(const std::string& master)
{
    // Asked as menus are drawn: nothing asked of the disk while there is
    // nothing linked.
    std::vector<ALMasterLink> out;
    if (ALMasterLinks* all = links(); all && !all->empty())
    {
        for (const ALMasterLink* one : all->mastering(canonical(master)))
        {
            out.push_back(*one);
        }
    }
    return out;
}

std::vector<ALMasterLink> ALScriptDiskMasters::all()
{
    ALMasterLinks* links_now = links();
    return links_now ? links_now->all() : std::vector<ALMasterLink>();
}

std::vector<ALMasterLink> ALScriptDiskMasters::linksIn(const LLUUID& object)
{
    std::vector<ALMasterLink> out;
    if (ALMasterLinks* links_now = links())
    {
        for (const ALMasterLink& one : links_now->all())
        {
            if (one.object == object)
            {
                out.push_back(one);
            }
        }
    }
    return out;
}

void ALScriptDiskMasters::link(ALMasterLink link)
{
    if (ALMasterLinks* all = links())
    {
        link.master = canonical(link.master);
        all->put(std::move(link));
        changed();
    }
}

void ALScriptDiskMasters::unlink(const ALScriptRef& ref)
{
    if (ALMasterLinks* all = links(); all && all->remove(ref.object, ref.item))
    {
        changed();
    }
}

void ALScriptDiskMasters::wrote(const std::string& path)
{
    for (const ALMasterLink& one : mastering(path))
    {
        send(ALScriptRef(one.object, one.item), ALMasterPlan::Send::Direct);
    }
}

void ALScriptDiskMasters::send(const ALScriptRef& ref, ALMasterPlan::Send kind)
{
    const std::optional<ALMasterLink> link = linkOf(ref);
    if (!link)
    {
        return;
    }
    // One at a time for a script, one more after it at most: a save of the
    // master asked while one is on its way goes up after it, with the
    // text the file has then. A save of the master outranks the studio's
    // own send asked beside it.
    const std::string id = ref.id();
    if (auto underway = mSending.find(id); underway != mSending.end())
    {
        underway->second = underway->second == ALMasterPlan::Send::Direct ? ALMasterPlan::Send::Direct : kind;
        return;
    }
    mSending.emplace(id, std::nullopt);
    ALScriptMasterUpload::start(*link, kind);
}

void ALScriptDiskMasters::finished(const Outcome& outcome, const std::optional<ALMasterLink>& updated)
{
    if (updated)
    {
        if (ALMasterLinks* all = links(); all && all->of(updated->object, updated->item))
        {
            // Unless let go of while it was on its way.
            all->put(*updated);
            changed();
        }
    }
    mOutcome(outcome);
    const std::string id = outcome.ref.id();
    if (auto underway = mSending.find(id); underway != mSending.end())
    {
        const std::optional<ALMasterPlan::Send> again = underway->second;
        mSending.erase(underway);
        if (again)
        {
            send(outcome.ref, *again);
        }
    }
}

void ALScriptDiskMasters::heardSaved(const ALScriptSaved& saved)
{
    // A send of a master's own is told as it ends; a notecard's is not yet
    // a master's to send.
    if (saved.sender.origin == ALScriptOrigin::Disk || saved.kind == ALScriptKind::Notecard || saved.asset.isNull())
    {
        return;
    }
    ALMasterLinks* all  = links();
    ALMasterLink*  link = all ? all->find(saved.ref.object, saved.ref.item) : nullptr;
    if (!link || saved.asset == link->base)
    {
        return;
    }
    // What went up the same as what the master last sent -- a recompile,
    // the VS Code plugin sending the same file -- moves on what the link
    // is of, and is nothing to say.
    if (!link->hash.empty() && hashOfWorld(saved.text, link->target) == link->hash)
    {
        link->base = saved.asset;
        save();
        return;
    }
    link->state = ALMasterLink::State::Differing;
    Outcome outcome;
    outcome.what     = Outcome::What::Differing;
    outcome.ref      = saved.ref;
    outcome.master   = link->master;
    outcome.itemName = link->itemName;
    outcome.by       = saved.sender.origin;
    changed();
    mOutcome(outcome);
}

// static
ALDiskIncludes ALScriptDiskMasters::blessedFor(const std::string& master, bool lua)
{
    ALScriptPreprocessor::Request request;
    request.path = master.empty() ? std::string() : "disk:" + master;
    request.lua  = lua;
    ALDiskIncludes blessed;
    for (const auto& [prefix, folder] : ALScriptPreprocessor::instance().moduleFolders(request))
    {
        blessed.bless(folder);
    }
    for (const ALScriptPreprocessor::StudioAlias& alias : ALScriptPreprocessor::studioAliases())
    {
        if (ALLuauConfig::absolute(alias.folder))
        {
            blessed.bless(alias.folder);
        }
    }
    return blessed;
}

// static
std::vector<std::pair<std::string, std::string>> ALScriptDiskMasters::aliasesFor(const std::string& master, bool lua)
{
    // A configuration's aliases, as `@name/` before their folder, and the
    // studio's own on disk.
    ALScriptPreprocessor::Request request;
    request.path = master.empty() ? std::string() : "disk:" + master;
    request.lua  = lua;
    // Each folder as the blessed ones are kept, its links followed, so that
    // what is joined to it is found under them as written.
    const auto canonical_folder = [](const std::string& folder) {
        ALDiskIncludes one;
        one.bless(folder);
        return one.folders().empty() ? std::string() : one.folders().front();
    };
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& [prefix, folder] : ALScriptPreprocessor::instance().moduleFolders(request))
    {
        if (prefix.size() > 2 && prefix.front() == '@' && prefix.back() == '/')
        {
            if (std::string kept = canonical_folder(folder); !kept.empty())
            {
                out.emplace_back(prefix.substr(1, prefix.size() - 2), std::move(kept));
            }
        }
    }
    for (const ALScriptPreprocessor::StudioAlias& alias : ALScriptPreprocessor::studioAliases())
    {
        if (ALLuauConfig::absolute(alias.folder))
        {
            if (std::string kept = canonical_folder(alias.folder); !kept.empty())
            {
                out.emplace_back(alias.name, std::move(kept));
            }
        }
    }
    return out;
}

// static
std::string ALScriptDiskMasters::fileLabel(const std::string& master, const ALDiskIncludes& blessed)
{
    for (const std::string& folder : blessed.folders())
    {
        if (ALDiskIncludes::lexicallyUnder(master, { folder }))
        {
            const std::string relative = ALLineLabel::relative(folder, master);
            if (!relative.empty())
            {
                return ALLineLabel::quotable(relative);
            }
        }
    }
    return std::string();
}

// static
LLInventoryItem* ALScriptDiskMasters::itemOf(const ALScriptRef& ref)
{
    if (ref.inInventory())
    {
        return gInventory.getItem(ref.item);
    }
    LLViewerObject* object = gObjectList.findObject(ref.object);
    return object ? object->getInventoryItem(ref.item) : nullptr;
}
