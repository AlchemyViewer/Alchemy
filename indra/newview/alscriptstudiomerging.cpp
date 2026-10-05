/**
 * @file alscriptstudiomerging.cpp
 * @brief A Script Studio window's merges: a save that came up against another, settled conflict by conflict.
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

#include "alscriptstudiomerging.h"

#include "alcodeeditor.h"
#include "aldiffedit.h"
#include "aldiffmerge.h"
#include "aldiffview.h"
#include "alscriptstudioservices.h"

ALScriptStudioMerging::ALScriptStudioMerging(ALScriptStudioServices& services, Window& window) : mServices(services), mWindow(window)
{
}

// static
bool ALScriptStudioMerging::canMerge(const Doc& doc)
{
    return doc.loaded && doc.modifiable && doc.editor && !doc.editor->isReadOnly();
}

void ALScriptStudioMerging::mergeSaved(Doc& doc)
{
    if (!doc.savedThere)
    {
        return;
    }
    // Merged, it is settled here from now on: the notice's take and keep
    // are done with. Only compared, they are still to be chosen from.
    const std::string theirs = *doc.savedThere;
    if (merge(doc, theirs, mServices.words("CompareSavedThere")))
    {
        doc.savedThere.reset();
        mWindow.refreshNotice();
    }
}

void ALScriptStudioMerging::mergeWorld(Doc& doc)
{
    const std::weak_ptr<bool> alive = mAlive;
    mWindow.loadWorld(doc, [this, alive](Doc& found, const std::string& text, const LLUUID& asset) {
        if (!alive.lock() || !found.loaded)
        {
            return;
        }
        // Merged, what the world holds is what the tab is made from now:
        // what was saved there is in the tab, and a save need not stop for
        // it.
        if (merge(found, text, mServices.words("CompareWorld")) && asset.notNull())
        {
            found.assetId = asset;
        }
    });
}

bool ALScriptStudioMerging::merge(Doc& doc, const std::string& theirs, const std::string& title)
{
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    // What both were: the text as last loaded or saved, where the undo
    // still reaches it.
    const std::optional<std::string> base = canMerge(doc) ? doc.editor->undoJournal().savedText() : std::nullopt;
    if (!base)
    {
        mWindow.compareWithTab(doc, theirs, title, std::string(), {});
        mServices.setStatus(mServices.words(canMerge(doc) ? "MergeNoBase" : "MergeReadOnly", args));
        return false;
    }
    becomes(doc, ALDiffMerge::start(*base, doc.editor->wholeText(), theirs));
    mWindow.compareWithTab(doc, theirs, title, std::string(), {});
    if (!doc.compareView)
    {
        return true;
    }
    doc.compareView->setMergeBase(*base);
    const S32 conflicts = doc.compareView->conflictCount();
    mServices.report(conflicts > 0 ? mServices.counted("MergeConflicts", conflicts, args) : mServices.words("MergeClean", args), false, &doc);
    return true;
}

// static
bool ALScriptStudioMerging::becomes(Doc& doc, const std::string& text)
{
    ALTextRange range;
    std::string put;
    std::string made;
    return ALDiffEdit::becoming(ALTextDiff::split(doc.editor->wholeText()), ALTextDiff::split(text), range, put, made) &&
           doc.editor->replaceAll({ { range, put } });
}
