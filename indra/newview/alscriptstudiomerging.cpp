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
        Doc::Merged merged;
        merged.savedThere = std::move(doc.savedThere);
        remember(doc, std::move(merged));
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
        const LLUUID was = found.assetId;
        if (merge(found, text, mServices.words("CompareWorld")))
        {
            if (asset.notNull())
            {
                found.assetId = asset;
            }
            Doc::Merged merged;
            merged.world       = true;
            merged.assetWas    = was;
            merged.assetMerged = found.assetId;
            remember(found, std::move(merged));
        }
    });
}

void ALScriptStudioMerging::textChanged(Doc& doc)
{
    // Only a step taken back or forward leaves one to step forward to:
    // typing throws those away, and so is never asked where the journal
    // stands, which would end its run.
    if (!doc.merged || !doc.editor || !doc.editor->undoJournal().canRedo())
    {
        return;
    }
    // Steps are numbered in the order they are made, and never again: one
    // older than the merge on top, and the merge has been undone.
    if (doc.editor->savePoint().serial >= doc.merged->serial)
    {
        return;
    }
    Doc::Merged merged = std::move(*doc.merged);
    doc.merged.reset();
    // Put back only where nothing has taken its place since: another save
    // landing meanwhile is the one to say.
    if (merged.savedThere && !doc.savedThere)
    {
        doc.savedThere = std::move(merged.savedThere);
    }
    if (merged.world && doc.assetId == merged.assetMerged)
    {
        doc.assetId = merged.assetWas;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc.name;
    const std::vector<std::string> actions = merged.world ? std::vector<std::string>{ "reload_world", "merge_world", "compare_world" }
                                                          : std::vector<std::string>{ "take_saved", "keep_saved", "merge_saved", "compare_saved" };
    mServices.report(mServices.words("MergeTakenBack", args), true, &doc, actions);
    mWindow.refreshNotice();
}

// static
bool ALScriptStudioMerging::canSettle(const Doc& doc)
{
    // As the bar lights its buttons: a merge, someone to make the edit, and
    // the caret in a conflict.
    const ALDiffView* view = doc.compareView;
    return view && doc.shownView() == Doc::View::Compare && view->merging() && view->canTakeBack() &&
           view->model().changeConflicts(view->changeAtCaret());
}

// static
bool ALScriptStudioMerging::settle(Doc& doc, ALTextMerge::Take take)
{
    return canSettle(doc) && doc.compareView->settle(doc.compareView->changeAtCaret(), take);
}

// static
void ALScriptStudioMerging::remember(Doc& doc, Doc::Merged merged)
{
    // The step the merge made is the newest. Where it put nothing in, the
    // newest is the one before it, and undoing that stands for undoing it:
    // the guard comes back sooner, never later.
    merged.serial = doc.editor->savePoint().serial;
    // A merge over one not yet saved keeps what that one set aside too.
    if (doc.merged)
    {
        if (!merged.savedThere)
        {
            merged.savedThere = std::move(doc.merged->savedThere);
        }
        if (doc.merged->world)
        {
            merged.assetMerged = merged.world ? merged.assetMerged : doc.merged->assetMerged;
            merged.assetWas    = doc.merged->assetWas;
            merged.world       = true;
        }
    }
    doc.merged = std::move(merged);
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
    // Conflicts left are said over the source until they are settled: the
    // comparison, which alone marks them, is left at a keystroke.
    if (conflicts > 0)
    {
        mServices.report(mServices.counted("MergeConflicts", conflicts, args), true, &doc, { "show_compare" });
    }
    else
    {
        mServices.report(mServices.words("MergeClean", args), false, &doc);
    }
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
