/**
 * @file alscriptstudiomasters.cpp
 * @brief A Script Studio window's side of scripts whose master is a file on disk.
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

#include "alscriptstudiomasters.h"

#include "alcodeeditor.h"
#include "alfilewrite.h"
#include "almastermatch.h"
#include "alscriptenvelope.h"
#include "alscriptstudioanalysis.h"
#include "alscriptstudioservices.h"
#include "alscriptworkspace.h"
#include "alwatchedfile.h"
#include "lldir.h"

namespace
{
    // A master named in what is said, by its file's name.
    std::string fileNameOf(const std::string& path)
    {
        return gDirUtilp ? gDirUtilp->getBaseFileName(path) : path;
    }

    // Who a change in the world came from, in the window's words.
    const char* whoSaid(ALScriptOrigin origin)
    {
        switch (origin)
        {
            case ALScriptOrigin::Bridge: return "SavedByBridge";
            case ALScriptOrigin::Editor: return "SavedByEditor";
            case ALScriptOrigin::Studio: return "SavedByStudio";
            default: return "SavedByQueue";
        }
    }
}

ALScriptStudioMasters::ALScriptStudioMasters(ALScriptStudioServices& services, ALScriptStudioAnalysis& analysis, Window& window)
: mServices(services), mAnalysis(analysis), mWindow(window)
{
    mOutcomeConnection = ALScriptDiskMasters::instance().onOutcome([this](const ALScriptDiskMasters::Outcome& outcome) { heard(outcome); });
}

// static
bool ALScriptStudioMasters::canLink(const Doc* doc)
{
    return doc && doc->file.empty() && !doc->notecard && doc->loaded && doc->modifiable && !doc->ref.isNull() &&
           !ALScriptDiskMasters::instance().linkOf(doc->ref);
}

bool ALScriptStudioMasters::mastersAny(const Doc* doc) const
{
    return doc && !doc->file.empty() && ALScriptDiskMasters::instance().masters(doc->file);
}

void ALScriptStudioMasters::linkToFile(Doc& doc)
{
    if (!canLink(&doc))
    {
        return;
    }
    const std::string id = doc.id;
    mWindow.pickMasterFile([this, id](const std::string& path) {
        Doc* found = mServices.findDoc(id);
        if (!found || !canLink(found) || !ofItsLanguage(*found, path))
        {
            return;
        }
        if (!found->editor->isDirty())
        {
            linkTo(*found, path, ALMasterLink::Made::Picked);
            return;
        }
        // What was typed here is in neither the world nor the file, and
        // the file's text is what the script will hold: the author asked,
        // the file named, whether it goes into the file, over what that
        // holds -- picking the file as the script's master is the consent
        // to that -- or is let go of. The tab found again by its id once
        // answered: it may have gone, or been linked, meanwhile.
        mWindow.askLinkUnsaved(*found, path, [this, id, path](Unsaved answer) {
            Doc* asked = answer == Unsaved::Cancel ? nullptr : mServices.findDoc(id);
            if (!asked || !canLink(asked) || !ofItsLanguage(*asked, path))
            {
                return;
            }
            if (answer == Unsaved::Discard)
            {
                // Linked as a tab with nothing typed is; what was typed is
                // set aside as the tab goes, as any thrown away is.
                linkTo(*asked, path, ALMasterLink::Made::Picked);
                return;
            }
            const bool unsaved = asked->editor->isDirty();
            if (!ALFileWrite::whole(path, asked->editor->wholeText()))
            {
                LLStringUtil::format_map_t args;
                args["[NAME]"] = asked->name;
                args["[FILE]"] = fileNameOf(path);
                mServices.report(mServices.words("MasterNotWritten", args), true, asked);
                return;
            }
            // Safe in the file, which its tab opens holding, to be sent by
            // its save: nothing of it set aside as the tab goes.
            asked->editor->resetDirty();
            linkTo(*asked, path, ALMasterLink::Made::Picked, unsaved);
        });
    });
}

bool ALScriptStudioMasters::ofItsLanguage(const Doc& doc, const std::string& path)
{
    const bool lua = doc.language.lua;
    if (ALDiskIncludes::extensionOf(path, ALDiskIncludes::scriptExtensions(lua)) > 0)
    {
        return true;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(path);
    mServices.report(mServices.words(lua ? "MasterNotSLuaFile" : "MasterNotLSLFile", args), true, &doc);
    return false;
}

void ALScriptStudioMasters::linkTo(Doc& doc, const std::string& path, ALMasterLink::Made made, bool written)
{
    if (!ofItsLanguage(doc, path))
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = fileNameOf(path);
    const bool   lua = doc.language.lua;
    ALMasterLink link;
    link.object     = doc.ref.object;
    link.item       = doc.ref.item;
    link.master     = path;
    link.made       = made;
    link.lua        = lua;
    link.target     = doc.language.compileTarget;
    link.base       = doc.assetId;
    link.itemName   = doc.name;
    link.objectName = doc.objectName;
    link.regionName = doc.regionName;
    link.linked     = LLDate::now();
    const ALScriptRef ref     = doc.ref;
    const bool        bridged = mWindow.heldByBridge(ref);
    // Whether the file is what the script is now: if not, the first save
    // of it sends it, what the world had kept in History. What was typed
    // here and written to it is not in the world.
    std::string on_disk;
    const bool  differs = written || !ALFileRead::whole(path, on_disk, ALDiskIncludes::MAX_BYTES) || on_disk != doc.editor->wholeText();
    ALScriptDiskMasters::instance().link(link);

    // The script is changed through its file from here: its tab gives way
    // to the file's.
    mWindow.closeTab(doc);
    mWindow.openMasterFile(path, lua);
    Doc* tab = masterTab(path);
    if (tab)
    {
        tab->master->offerFor = ref;
    }
    std::string said = mServices.words("MasterLinked", args);
    if (bridged)
    {
        said += " " + mServices.words("MasterLinkedBridge", args);
    }
    if (differs)
    {
        said += " " + mServices.words("MasterLinkDiffers", args);
    }
    mServices.report(said, bridged, tab, differs && tab ? std::vector<std::string>{ "master_send", "master_compare" } : std::vector<std::string>());
}

void ALScriptStudioMasters::unlink(Doc& doc)
{
    std::vector<ALScriptRef> refs;
    if (!doc.file.empty())
    {
        for (const ALMasterLink& one : ALScriptDiskMasters::instance().mastering(doc.file))
        {
            refs.emplace_back(one.object, one.item);
        }
    }
    else if (ALScriptDiskMasters::instance().linkOf(doc.ref))
    {
        refs.push_back(doc.ref);
    }
    for (const ALScriptRef& ref : refs)
    {
        const std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(ref);
        if (!link)
        {
            continue;
        }
        ALScriptDiskMasters::instance().unlink(ref);
        LLStringUtil::format_map_t args;
        args["[NAME]"] = link->itemName;
        args["[FILE]"] = fileNameOf(link->master);
        mServices.report(mServices.words("MasterUnlinked", args), false, &doc);
    }
}

void ALScriptStudioMasters::sendFromFile(Doc& doc)
{
    if (!mastersAny(&doc))
    {
        return;
    }
    // What goes up is the file on disk: what was typed here, saved first.
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    if (doc.editor->isDirty())
    {
        mServices.report(mServices.words("MasterSaveFileFirst", args), true, &doc);
        return;
    }
    const std::vector<ALMasterLink> links = ALScriptDiskMasters::instance().mastering(doc.file);
    mServices.setStatus(mServices.counted("MasterSending", static_cast<S32>(links.size()), args));
    for (const ALMasterLink& one : links)
    {
        ALScriptDiskMasters::instance().send(ALScriptRef(one.object, one.item), ALMasterPlan::Send::Derived);
    }
}

bool ALScriptStudioMasters::openMaster(const ALScriptRef& ref, const std::string& name)
{
    const std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(ref);
    if (!link || link->state == ALMasterLink::State::Suspended || !ALFileStamp::of(link->master).exists)
    {
        return false;
    }
    mWindow.openMasterFile(link->master, link->lua);
    LLStringUtil::format_map_t args;
    args["[NAME]"] = name.empty() ? link->itemName : name;
    args["[FILE]"] = fileNameOf(link->master);
    mServices.setStatus(mServices.words("MasterOpened", args));
    return true;
}

void ALScriptStudioMasters::loaded(Doc& doc)
{
    if (!canLink(&doc))
    {
        return;
    }
    // A file the script names as its own, offered where a hint may reach
    // it: under the folders its includes may be read from, and nowhere
    // else, whatever the script says.
    const bool                       lua  = doc.language.lua;
    const std::string                head = doc.envelope ? doc.envelope->header : std::string();
    const std::optional<std::string> hint = ALMasterMatch::hintOf(doc.editor->wholeText(), head, lua);
    if (!hint)
    {
        return;
    }
    std::string                      why;
    const std::optional<std::string> found =
        ALMasterMatch::resolve(*hint, ALScriptDiskMasters::blessedFor(std::string(), lua), ALScriptDiskMasters::aliasesFor(std::string(), lua),
                               lua, why);
    if (!found)
    {
        return;
    }
    doc.master->hinted = *found;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    args["[FILE]"] = *found;
    mServices.report(mServices.words("MasterHinted", args), false, &doc, { "master_link_hint" });
}

void ALScriptStudioMasters::fileSaved(Doc& doc)
{
    if (doc.file.empty())
    {
        return;
    }
    if (mastersAny(&doc))
    {
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        mServices.setStatus(mServices.counted("MasterSending", static_cast<S32>(ALScriptDiskMasters::instance().mastering(doc.file).size()), args));
    }
    // What it masters sent, and the scripts that include it sent again --
    // those no tab holds among them -- whether it masters anything or not.
    ALScriptDiskMasters::instance().wrote(doc.file);
}

void ALScriptStudioMasters::sayUnheard()
{
    for (const ALScriptDiskMasters::Outcome& outcome : ALScriptDiskMasters::instance().takeUnheard())
    {
        heard(outcome);
    }
}

void ALScriptStudioMasters::offer(Doc& doc, const std::string& action)
{
    const ALScriptRef ref = doc.file.empty() ? doc.ref : doc.master->offerFor;
    if (action == "master_link_hint")
    {
        if (!doc.master->hinted.empty() && canLink(&doc))
        {
            linkTo(doc, doc.master->hinted, ALMasterLink::Made::Hint);
        }
    }
    else if (action == "master_send" && !ref.isNull())
    {
        // Asked for outright: sent as a save of the master is, over a
        // change in the world, which is kept first.
        ALScriptDiskMasters::instance().send(ref, ALMasterPlan::Send::Direct);
    }
    else if (action == "master_compare" && !ref.isNull())
    {
        compareWithWorld(doc, ref);
    }
    else if (action == "master_unlink" && !ref.isNull())
    {
        if (const std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(ref))
        {
            ALScriptDiskMasters::instance().unlink(ref);
            LLStringUtil::format_map_t args;
            args["[NAME]"] = link->itemName;
            args["[FILE]"] = fileNameOf(link->master);
            mServices.report(mServices.words("MasterUnlinked", args), false, &doc);
        }
    }
    else if (action == "master_find" && !ref.isNull())
    {
        // Its master found again where it went: the link kept, its file the
        // one picked.
        mWindow.pickMasterFile([this, ref](const std::string& path) {
            std::optional<ALMasterLink> link = ALScriptDiskMasters::instance().linkOf(ref);
            if (!link)
            {
                return;
            }
            link->master = path;
            link->state  = ALMasterLink::State::Active;
            ALScriptDiskMasters::instance().link(*link);
            LLStringUtil::format_map_t args;
            args["[NAME]"] = link->itemName;
            args["[FILE]"] = fileNameOf(path);
            mWindow.openMasterFile(path, link->lua);
            mServices.report(mServices.words("MasterRelinked", args), false, masterTab(path));
        });
    }
}

void ALScriptStudioMasters::compareWithWorld(Doc& doc, const ALScriptRef& ref)
{
    const std::string id = doc.id;
    ALScriptWorkspace::instance().load(ref, [this, id](const ALScriptLoaded& loaded) {
        Doc* found = mServices.findDoc(id);
        if (!found || !loaded.error.empty())
        {
            return;
        }
        // The author's text as it is in the world, out of its envelope.
        std::string theirs = loaded.text;
        if (const std::optional<ALScriptEnvelope> envelope = ALScriptEnvelope::parse(loaded.text))
        {
            theirs = envelope->source;
        }
        LLStringUtil::format_map_t args;
        args["[NAME]"] = loaded.name;
        mWindow.compare(*found, found->editor->wholeText(), theirs, mServices.words("CompareMasterFile", args),
                        mServices.words("CompareMasterWorld", args));
    });
}

ALScriptStudioMasters::Doc* ALScriptStudioMasters::masterTab(const std::string& master) const
{
    return mServices.findDoc("disk:" + master);
}

void ALScriptStudioMasters::heard(const ALScriptDiskMasters::Outcome& outcome)
{
    typedef ALScriptDiskMasters::Outcome::What What;
    LLStringUtil::format_map_t args;
    args["[NAME]"] = outcome.itemName;
    args["[FILE]"] = fileNameOf(outcome.master);
    args["[WHY]"]  = outcome.why;
    // Said on the master's tab where it is open, which its offers act on.
    Doc* tab = masterTab(outcome.master);
    if (tab)
    {
        tab->master->offerFor = outcome.ref;
    }
    const auto said = [&](const char* key, bool failure, std::vector<std::string> actions = {}) {
        mServices.report(mServices.words(key, args), failure, tab, tab ? actions : std::vector<std::string>());
    };
    switch (outcome.what)
    {
        case What::Sent:
        {
            const bool compiled = outcome.result && outcome.result->success;
            // What the compiler said, in the master's tab, through the map
            // the send went up with.
            if (tab && outcome.result)
            {
                tab->problems = Doc::compiledOf(outcome.result->diagnostics, outcome.result->sourceMap.get(), outcome.result->codeLine);
                const std::vector<Doc::Compiled> found = Doc::compiledOf(outcome.preprocessed, nullptr, 0);
                tab->problems.insert(tab->problems.end(), found.begin(), found.end());
                mAnalysis.refreshProblems(*tab);
            }
            std::string text = mServices.words(compiled ? "MasterSent" : "MasterNotCompiled", args);
            if (outcome.keptTheirs)
            {
                text += " " + mServices.words("MasterKeptTheirs", args);
            }
            mServices.report(text, !compiled, tab);
            return;
        }
        case What::Failed:
            // Not expanded: why, in the master's tab.
            if (tab && !outcome.preprocessed.empty())
            {
                tab->problems = Doc::compiledOf(outcome.preprocessed, nullptr, 0);
                mAnalysis.refreshProblems(*tab);
            }
            said("MasterSendFailed", true, { "master_send" });
            return;
        case What::Skipped: said("MasterSkipped", false); return;
        case What::Held: said("MasterHeld", true, { "master_send", "master_compare", "master_unlink" }); return;
        case What::Differing:
            args["[WHO]"] = mServices.words(whoSaid(outcome.by));
            said("MasterDiffering", true, { "master_send", "master_compare", "master_unlink" });
            return;
        case What::Suspended: said("MasterSuspended", true, { "master_find", "master_unlink" }); return;
        case What::Orphaned: said("MasterOrphaned", false, { "master_unlink" }); return;
        case What::Pending: said("MasterPending", false, { "master_send" }); return;
    }
}
