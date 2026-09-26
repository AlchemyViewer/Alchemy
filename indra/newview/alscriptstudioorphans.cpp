/**
 * @file alscriptstudioorphans.cpp
 * @brief Script Studio's orphans: tabs whose script is gone, out of reach or not loaded, and the notice over the editor.
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

#include "alfloaterscriptstudio.h"

#include "alscriptnotecardtab.h"
#include "alscriptrecovery.h"
#include "llappviewer.h"
#include "llbutton.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "lltextbox.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"

#include <algorithm>

ALFloaterScriptStudio::Doc::Orphan ALFloaterScriptStudio::orphanOf(const Doc& doc) const
{
    if (!doc.loaded)
    {
        return Doc::Orphan::None;
    }
    if (!doc.file.empty())
    {
        return LLFile::isfile(doc.file) ? Doc::Orphan::None : Doc::Orphan::FileGone;
    }
    if (doc.ref.isNull())
    {
        return Doc::Orphan::None;
    }
    if (gDisconnected)
    {
        return Doc::Orphan::Offline;
    }
    // A kept text over a script that may no longer be changed, or that
    // could not be loaded, stays that while there is an item: nothing here
    // says it has changed, and loading it again is tried on its own terms.
    const auto held = [&doc](Doc::Orphan seen) {
        const bool stays = doc.orphan == Doc::Orphan::Locked || doc.orphan == Doc::Orphan::Unloaded;
        return stays && (seen == Doc::Orphan::None || seen == Doc::Orphan::Trashed) ? doc.orphan : seen;
    };
    if (doc.ref.inInventory())
    {
        if (!gInventory.getItem(doc.ref.item))
        {
            return Doc::Orphan::Removed;
        }
        const LLUUID trash = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
        return held(trash.notNull() && gInventory.isObjectDescendentOf(doc.ref.item, trash) ? Doc::Orphan::Trashed : Doc::Orphan::None);
    }
    LLViewerObject* object = gObjectList.findObject(doc.ref.object);
    if (!object || object->isDead())
    {
        return Doc::Orphan::Away;
    }
    // Gone from its object where the region has said what the prim holds;
    // while that is being asked again, as it was.
    for (const ALScriptExplorerModel::Object& one : mExplorerPane->model().objects())
    {
        for (const ALScriptExplorerModel::Prim& prim : one.prims)
        {
            if (prim.id != doc.ref.object)
            {
                continue;
            }
            if (!prim.fetched)
            {
                return held(doc.orphan == Doc::Orphan::Removed ? Doc::Orphan::Removed : Doc::Orphan::None);
            }
            const bool there = std::any_of(prim.items.begin(), prim.items.end(), [&doc](const ALScriptWorkspace::Item& item) { return item.id == doc.ref.item; });
            return held(there ? Doc::Orphan::None : Doc::Orphan::Removed);
        }
    }
    return held(doc.orphan == Doc::Orphan::Removed ? Doc::Orphan::Removed : Doc::Orphan::None);
}

ALFloaterScriptStudio::Doc::Orphan ALFloaterScriptStudio::failedAs(const Doc& doc, ALScriptWorkspace::Loaded::Failure failure) const
{
    using Failure = ALScriptWorkspace::Loaded::Failure;
    switch (failure)
    {
        case Failure::NotPermitted:
            return Doc::Orphan::Locked;
        case Failure::Unreadable:
        case Failure::Fetch:
            return Doc::Orphan::Unloaded;
        default:
            break;
    }
    // Gone: from the inventory or its object, or its object out of sight.
    if (doc.ref.inInventory())
    {
        return Doc::Orphan::Removed;
    }
    LLViewerObject* object = gObjectList.findObject(doc.ref.object);
    return object && !object->isDead() ? Doc::Orphan::Removed : Doc::Orphan::Away;
}

void ALFloaterScriptStudio::reattach(Doc& doc)
{
    // What the tab holds carried over what the item has, with its history,
    // as a kept text is taken up: the item loaded under it at last, so that
    // what it is saved as is what the item is -- its language and target,
    // whether it runs, whether it may be changed, the items a notecard's
    // asset carries. Written first, so that nothing typed is only in the
    // tab while it loads.
    mRecovery.keep(doc);
    ALScriptRecoveryEntry holding = ALScriptStudioRecovery::entryOf(doc);
    doc.detached                  = false;
    doc.recovering                = holding;
    doc.carriedText               = holding.text;
    ALScriptNotecardTab::carry(doc, doc);
    doc.loaded = false;
    doc.editor->setReadOnly(true);
    const LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().load(doc.ref, [handle](const ALScriptWorkspace::Loaded& answer) {
        if (ALFloaterScriptStudio* studio = ALViewType::as<ALFloaterScriptStudio>(handle.get()))
        {
            studio->loaded(answer);
        }
    });
}

void ALFloaterScriptStudio::checkOrphans()
{
    bool changed = false;
    for (std::unique_ptr<Doc>& each : mDocs)
    {
        Doc& doc = *each;
        // Where it is, while it is in sight, for a kept text to say later.
        if (LLViewerObject* object = doc.ref.inInventory() || !doc.file.empty() ? nullptr : gObjectList.findObject(doc.ref.object))
        {
            LLViewerObject* root = object->getRootEdit() ? object->getRootEdit() : object;
            doc.objectName       = ALScriptWorkspace::objectName(root, doc.objectName);
            if (object->getRegion())
            {
                doc.regionName = object->getRegion()->getName();
            }
        }
        // Renamed where it lives since it was opened -- in the inventory, in
        // its object: called so here too.
        if (doc.loaded && doc.file.empty() && !doc.ref.isNull())
        {
            LLViewerObject*        holder = doc.ref.inInventory() ? nullptr : gObjectList.findObject(doc.ref.object);
            const LLInventoryItem* item   = doc.ref.inInventory() ? gInventory.getItem(doc.ref.item)
                                            : holder              ? holder->getInventoryItem(doc.ref.item)
                                                                  : nullptr;
            if (item && !item->getName().empty())
            {
                renameDoc(doc, item->getName());
            }
        }
        const Doc::Orphan was       = doc.orphan;
        const Doc::Orphan now_seen  = orphanOf(doc);
        // Out of sight for a moment is not gone: an object at the edge of
        // what is in view comes and goes, and a region crossing takes it
        // away and gives it back.
        constexpr F64 AWAY_AFTER = 3.0;
        if (now_seen == Doc::Orphan::Away && was != Doc::Orphan::Away)
        {
            const F64 now = LLTimer::getTotalSeconds();
            if (doc.awaySince <= 0.0)
            {
                doc.awaySince = now;
            }
            if (now - doc.awaySince < AWAY_AFTER)
            {
                continue;
            }
        }
        else if (now_seen != Doc::Orphan::Away)
        {
            doc.awaySince = 0.0;
        }
        doc.orphan = now_seen;
        if (doc.orphan == was)
        {
            continue;
        }
        changed             = true;
        doc.noticeDismissed = false;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        const bool lost = doc.orphan == Doc::Orphan::Away || doc.orphan == Doc::Orphan::Removed;
        if (lost && doc.modifiable && doc.editor->isDirty())
        {
            // What was typed is on disk now, not only in the tab, and the
            // Output says what can be done with it.
            mRecovery.keep(doc);
            report(getString(doc.orphan == Doc::Orphan::Away ? "OrphanAwayKept" : "OrphanRemovedKept", args), true, &doc,
                   doc.file.empty() ? std::vector<std::string>{ "copy", "export" } : std::vector<std::string>{ "export" });
        }
        else if ((was == Doc::Orphan::Away || was == Doc::Orphan::Removed || was == Doc::Orphan::Offline) && doc.orphan == Doc::Orphan::None &&
                 !doc.detached)
        {
            // A detached tab is loaded now, and what the load says is said.
            report(getString("OrphanBack", args), false, &doc);
        }
    }
    // Detached tabs whose item is in reach loaded under what they hold, and
    // a fetch that failed on the way tried again -- each only once its next
    // try is due, so that a load that fails however often it is tried is
    // tried a few times, further apart each time, and then waits for a
    // person to ask. Loaded once the walk is done, since an answer can come
    // at once.
    const F64                now = LLTimer::getTotalSeconds();
    std::vector<std::string> reattaching;
    for (const std::unique_ptr<Doc>& each : mDocs)
    {
        const Doc& doc   = *each;
        const bool ready = doc.orphan == Doc::Orphan::None ||
                           (doc.orphan == Doc::Orphan::Unloaded && doc.loadFailure == ALScriptWorkspace::Loaded::Failure::Fetch);
        if (doc.detached && doc.loaded && ready && ALScriptRecoveryRetry::mayTry(doc.reattachTries) && now >= doc.nextReattach)
        {
            reattaching.push_back(doc.id);
        }
    }
    for (const std::string& id : reattaching)
    {
        if (const size_t index = indexOf(id); index != NONE)
        {
            reattach(*mDocs[index]);
        }
    }
    if (changed)
    {
        refreshNotice();
        refreshToolbar();
    }
}

void ALFloaterScriptStudio::refreshNotice()
{
    if (!mNoticePanel)
    {
        return;
    }
    // What the tab in front has to reckon with, the most pressing first,
    // and up to two things to be done about it: each an action and the
    // name of its words, whose tip is the same name with Tip after it.
    Doc*        doc = active();
    std::string text;
    std::pair<std::string, std::string> buttons[2];
    if (doc && doc->recoverable)
    {
        // Said so where the script was saved since the text was kept.
        LLStringUtil::format_map_t args;
        args["[WHEN]"]   = doc->recoverable->whenSaid();
        const bool stale = doc->recoverable->baseAsset.notNull() && doc->assetId.notNull() && doc->recoverable->baseAsset != doc->assetId;
        text             = getString(stale ? "NoticeRecoverableStale" : "NoticeRecoverable", args);
        buttons[0]       = { "restore", "NoticeRestore" };
        buttons[1]       = { "discard_left", "NoticeDiscard" };
    }
    else if (doc && !doc->noticeDismissed)
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = doc->file;
        switch (doc->orphan)
        {
            case Doc::Orphan::Away:
                text       = getString("NoticeAway");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Removed:
                text       = getString(doc->ref.inInventory() ? "NoticeRemovedInventory" : "NoticeRemoved");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Offline:
                text       = getString("NoticeOffline");
                buttons[0] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Trashed:
                text = getString("NoticeTrashed");
                break;
            case Doc::Orphan::Locked:
                text       = getString("NoticeLocked");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Doc::Orphan::Unloaded:
            {
                LLStringUtil::format_map_t why;
                why["[ERROR]"] = doc->loadError;
                text           = getString("NoticeUnloaded", why);
                buttons[0]     = { "retry_load", "NoticeTryAgain" };
                buttons[1]     = { "copy", "NoticeCopy" };
                break;
            }
            case Doc::Orphan::FileGone:
                text       = getString("NoticeFileGone", args);
                buttons[0] = { "save", "NoticeSaveAgain" };
                break;
            default:
                break;
        }
    }
    mNoticePanel->setVisible(!text.empty());
    if (text.empty())
    {
        return;
    }
    mNoticeText->setText(text);
    mNoticeText->setToolTip(text);
    // The buttons as wide as their words, from the right, the way out
    // last; the words have what is left.
    const LLFontGL* font  = LLFontGL::getFontSansSerifSmall();
    S32             right = mNoticePanel->getRect().getWidth() - 4 - 22 - 6;
    LLButton*       shown[2] = { mNoticeFirst, mNoticeSecond };
    for (S32 i = 1; i >= 0; --i)
    {
        LLButton*   button = shown[i];
        const auto& [id, label] = buttons[i];
        mNoticeActions[i]       = id;
        button->setVisible(!id.empty());
        if (id.empty())
        {
            continue;
        }
        const std::string said  = getString(label);
        const S32         width = font->getWidth(said) + 24;
        button->setLabel(said);
        button->setToolTip(getString(label + "Tip"));
        const LLRect was = button->getRect();
        button->setShape(LLRect(right - width, was.mTop, right, was.mBottom));
        right -= width + 4;
    }
    const LLRect words = mNoticeText->getRect();
    mNoticeText->setShape(LLRect(words.mLeft, words.mTop, llmax(words.mLeft + 40, right - 6), words.mBottom));
}

void ALFloaterScriptStudio::onNoticeAction(const std::string& action)
{
    Doc* doc = active();
    if (!doc)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc->name;
    if (action == "close")
    {
        // Hidden until there is something else to say; a kept text from an
        // earlier session stays offered, under File > Recover Unsaved
        // Changes, once the notice is gone.
        doc->noticeDismissed = true;
        doc->recoverable.reset();
    }
    else if (action == "restore" && doc->recoverable)
    {
        const ALScriptRecoveryEntry entry = *doc->recoverable;
        doc->recoverable.reset();
        mRecovery.takeUp(*doc, entry);
        // A tab left holding it on its own has said why instead.
        if (doc->orphan == Doc::Orphan::None)
        {
            report(getString("RecoveryRestored", args), false, doc);
        }
    }
    else if (action == "discard_left" && doc->recoverable)
    {
        if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store())
        {
            store->discard(*doc->recoverable);
        }
        doc->recoverable.reset();
        report(getString("RecoveryDiscarded", args), false, doc);
    }
    else if (action == "retry_load" && doc->detached && doc->loaded)
    {
        // Asked for: tried now, and a few more times after if it fails.
        doc->reattachTries = 0;
        reattach(*doc);
    }
    else if (action == "copy")
    {
        saveCopyToInventory(*doc);
    }
    else if (action == "export")
    {
        mFiles.saveCopy();
    }
    else if (action == "save")
    {
        mSaving.saveAsked(*doc);
    }
    refreshNotice();
}
