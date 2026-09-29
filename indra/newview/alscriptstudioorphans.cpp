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

#include "alscriptstudioorphans.h"

#include "alcodeeditor.h"
#include "alrecoverystore.h"
#include "alscriptexternaleditor.h"
#include "alscriptstudiorecovery.h"
#include "alscriptstudiosaves.h"
#include "alscriptstudioservices.h"
#include "alscriptstudiotabs.h"
#include "lltimer.h"

#include <algorithm>

#include <vector>

namespace
{
    typedef ALScriptStudioDoc Doc;

    // Whether the notice says what a tab is: something, and not hidden.
    bool orphanSaid(const Doc& doc)
    {
        return !doc.orphan->noticeDismissed && doc.orphan->kind != Doc::Orphan::None;
    }

    // What of a tab's offer can still be done, each with the notice's words
    // for it: one of two texts taken only while there is still another.
    std::vector<std::pair<std::string, std::string>> stillOffered(const Doc& doc)
    {
        std::vector<std::pair<std::string, std::string>> out;
        if (!doc.offer)
        {
            return out;
        }
        for (const std::string& action : doc.offer->actions)
        {
            const bool external = action == "take_external" || action == "keep_here";
            const bool there    = action == "take_saved" || action == "keep_saved" || action == "compare_saved";
            if ((external && !doc.external->waiting) || (there && !doc.savedThere))
            {
                continue;
            }
            const char* label = action == "save_anyway"     ? "NoticeSaveAnyway"
                                : action == "retry"         ? "NoticeRetrySave"
                                : action == "copy"          ? "NoticeCopy"
                                : action == "export"        ? "NoticeExport"
                                : action == "reload_world"  ? "NoticeReload"
                                : action == "take_external" ? "NoticeTakeExternal"
                                : action == "keep_here"     ? "NoticeKeepHere"
                                : action == "take_saved"    ? "NoticeTakeSaved"
                                : action == "keep_saved"    ? "NoticeKeepSaved"
                                : action == "compare_saved" || action == "compare_world" ? "NoticeCompare"
                                                                                         : nullptr;
            if (label)
            {
                out.emplace_back(action, label);
            }
        }
        return out;
    }

    // Whether the notice says a tab's offer: nothing more pressing to say,
    // and something of it still to be done.
    bool offerSaid(const Doc& doc)
    {
        return !doc.recoverable && !orphanSaid(doc) && !stillOffered(doc).empty();
    }
}

ALScriptStudioOrphans::ALScriptStudioOrphans(ALScriptStudioServices& services, ALScriptStudioTabs& tabs, ALScriptStudioSaves& saves, ALScriptStudioRecovery& recovery, Window& window) : mServices(services), mTabs(tabs), mSaves(saves), mRecovery(recovery), mWindow(window)
{
}

// static
ALScriptStudioOrphans::Orphan ALScriptStudioOrphans::seen(const Doc& doc, const Reach& reach)
{
    if (!doc.loaded)
    {
        return Orphan::None;
    }
    if (!doc.file.empty())
    {
        return reach.fileThere ? Orphan::None : Orphan::FileGone;
    }
    if (doc.ref.isNull())
    {
        return Orphan::None;
    }
    if (reach.offline)
    {
        return Orphan::Offline;
    }
    // A kept text over a script that may no longer be changed, or that
    // could not be loaded, stays that while there is an item: nothing here
    // says it has changed, and loading it again is tried on its own terms.
    const auto held = [&doc](Orphan seen) {
        const bool stays = doc.orphan->kind == Orphan::Locked || doc.orphan->kind == Orphan::Unloaded;
        return stays && (seen == Orphan::None || seen == Orphan::Trashed) ? doc.orphan->kind : seen;
    };
    if (doc.ref.inInventory())
    {
        if (!reach.itemThere)
        {
            return Orphan::Removed;
        }
        return held(reach.trashed ? Orphan::Trashed : Orphan::None);
    }
    if (!reach.objectThere)
    {
        return Orphan::Away;
    }
    // Gone from its object where the region has said what the prim holds;
    // while that is being asked again, as it was.
    if (!reach.heldByPrim)
    {
        return held(doc.orphan->kind == Orphan::Removed ? Orphan::Removed : Orphan::None);
    }
    return held(*reach.heldByPrim ? Orphan::None : Orphan::Removed);
}

// static
ALScriptStudioOrphans::Orphan ALScriptStudioOrphans::failedAs(const Doc& doc, ALScriptLoaded::Failure failure, bool object_there)
{
    using Failure = ALScriptLoaded::Failure;
    switch (failure)
    {
        case Failure::NotPermitted:
            return Orphan::Locked;
        case Failure::Unreadable:
        case Failure::Fetch:
            return Orphan::Unloaded;
        default:
            break;
    }
    // Gone: from the inventory or its object, or its object out of sight.
    if (doc.ref.inInventory())
    {
        return Orphan::Removed;
    }
    return object_there ? Orphan::Removed : Orphan::Away;
}

// static
void ALScriptStudioOrphans::loadFailed(Doc& doc)
{
    doc.orphan->nextReattach = LLTimer::getTotalSeconds() + ALRecoveryRetry::delayAfter(++doc.orphan->reattachTries);
}

void ALScriptStudioOrphans::reattach(Doc& doc)
{
    // What the tab holds carried over what the item has, with its history,
    // as a kept text is taken up: the item loaded under it at last, so that
    // what it is saved as is what the item is -- its language and target,
    // whether it runs, whether it may be changed, the items a notecard's
    // asset carries. Written first, so that nothing typed is only in the
    // tab while it loads.
    mRecovery.keep(doc);
    ALRecoveryEntry holding = ALScriptStudioRecovery::entryOf(doc);
    doc.orphan->detached = false;
    doc.recovering       = holding;
    doc.carriedText      = holding.text;
    doc.carryItemsTo(doc);
    doc.loaded = false;
    doc.editor->setReadOnly(true);
    mWindow.loadScript(doc.ref);
}

void ALScriptStudioOrphans::retryLoad(Doc& doc)
{
    // Asked for: tried now, and a few more times after if it fails.
    doc.orphan->reattachTries = 0;
    reattach(doc);
}

F64 ALScriptStudioOrphans::check()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_SCRIPTDEV;
    bool changed = false;
    // The soonest a tab is to be looked at again for time alone.
    F64        due     = 0.0;
    const auto soonest = [&due](F64 at) { due = due <= 0.0 ? at : std::min(due, at); };
    for (Doc* each : mServices.openDocs())
    {
        Doc& doc = *each;
        // Where it is, and what it is called, while it is in sight.
        mWindow.refreshPlace(doc);
        const Orphan was      = doc.orphan->kind;
        const Orphan now_seen = seen(doc, mWindow.reach(doc));
        // Out of sight for a moment is not gone: an object at the edge of
        // what is in view comes and goes, and a region crossing takes it
        // away and gives it back.
        constexpr F64 AWAY_AFTER = 3.0;
        if (now_seen == Orphan::Away && was != Orphan::Away)
        {
            const F64 now = LLTimer::getTotalSeconds();
            if (doc.orphan->awaySince <= 0.0)
            {
                doc.orphan->awaySince = now;
            }
            if (now - doc.orphan->awaySince < AWAY_AFTER)
            {
                soonest(doc.orphan->awaySince + AWAY_AFTER);
                continue;
            }
        }
        else if (now_seen != Orphan::Away)
        {
            doc.orphan->awaySince = 0.0;
        }
        doc.orphan->kind = now_seen;
        if (doc.orphan->kind == was)
        {
            continue;
        }
        changed                     = true;
        doc.orphan->noticeDismissed = false;
        LLStringUtil::format_map_t args;
        args["[NAME]"] = doc.name;
        const bool lost = doc.orphan->kind == Orphan::Away || doc.orphan->kind == Orphan::Removed;
        if (lost && doc.modifiable && doc.editor->isDirty())
        {
            // What was typed is on disk now, not only in the tab, and the
            // Output says what can be done with it.
            mRecovery.keep(doc);
            mServices.report(mServices.words(doc.orphan->kind == Orphan::Away ? "OrphanAwayKept" : "OrphanRemovedKept", args), true, &doc,
                             doc.file.empty() ? std::vector<std::string>{ "copy", "export" } : std::vector<std::string>{ "export" });
        }
        else if ((was == Orphan::Away || was == Orphan::Removed || was == Orphan::Offline) && doc.orphan->kind == Orphan::None &&
                 !doc.orphan->detached)
        {
            // A detached tab is loaded now, and what the load says is said.
            mServices.report(mServices.words("OrphanBack", args), false, &doc);
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
    for (const Doc* each : mServices.openDocs())
    {
        const Doc& doc   = *each;
        const bool ready = doc.orphan->kind == Orphan::None ||
                           (doc.orphan->kind == Orphan::Unloaded && doc.loadFailure == ALScriptLoaded::Failure::Fetch);
        const bool may = ALRecoveryRetry::mayTry(doc.orphan->reattachTries);
        if (doc.orphan->detached && doc.loaded && ready && may)
        {
            if (now >= doc.orphan->nextReattach)
            {
                reattaching.push_back(doc.id);
            }
            else
            {
                soonest(doc.orphan->nextReattach);
            }
        }
    }
    for (const std::string& id : reattaching)
    {
        if (Doc* doc = mServices.findDoc(id))
        {
            reattach(*doc);
        }
    }
    if (changed)
    {
        refreshNotice();
        mTabs.refreshToolbar();
    }
    return due;
}

// static
ALScriptNoticeBar::Notice ALScriptStudioOrphans::noticeFor(const Doc* doc, const ALScriptStudioServices& services)
{
    // What the tab has to reckon with, the most pressing first, and up to
    // two things to be done about it.
    ALScriptNoticeBar::Notice notice;
    std::string&              text    = notice.text;
    auto&                     buttons = notice.buttons;
    if (doc && doc->recoverable)
    {
        // Said so where the script was saved since the text was kept.
        LLStringUtil::format_map_t args;
        args["[WHEN]"]   = doc->recoverable->whenSaid();
        const bool stale = doc->recoverable->baseAsset.notNull() && doc->assetId.notNull() && doc->recoverable->baseAsset != doc->assetId;
        text             = services.words(stale ? "NoticeRecoverableStale" : "NoticeRecoverable", args);
        buttons[0]       = { "compare_kept", "NoticeCompare" };
        buttons[1]       = { "restore", "NoticeRestore" };
        buttons[2]       = { "discard_left", "NoticeDiscard" };
    }
    else if (doc && orphanSaid(*doc))
    {
        LLStringUtil::format_map_t args;
        args["[FILE]"] = doc->file;
        switch (doc->orphan->kind)
        {
            case Orphan::Away:
                text       = services.words("NoticeAway");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Orphan::Removed:
                text       = services.words(doc->ref.inInventory() ? "NoticeRemovedInventory" : "NoticeRemoved");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Orphan::Offline:
                text       = services.words("NoticeOffline");
                buttons[0] = { "export", "NoticeExport" };
                break;
            case Orphan::Trashed:
                text = services.words("NoticeTrashed");
                break;
            case Orphan::Locked:
                text       = services.words("NoticeLocked");
                buttons[0] = { "copy", "NoticeCopy" };
                buttons[1] = { "export", "NoticeExport" };
                break;
            case Orphan::Unloaded:
            {
                LLStringUtil::format_map_t why;
                why["[ERROR]"] = doc->loadError;
                text           = services.words("NoticeUnloaded", why);
                buttons[0]     = { "retry_load", "NoticeTryAgain" };
                buttons[1]     = { "copy", "NoticeCopy" };
                break;
            }
            case Orphan::FileGone:
                text       = services.words("NoticeFileGone", args);
                buttons[0] = { "save", "NoticeSaveAgain" };
                break;
            default:
                break;
        }
    }
    else if (doc && offerSaid(*doc))
    {
        // What the last word about it offered, as Output's links do.
        text                                                 = doc->offer->text;
        const std::vector<std::pair<std::string, std::string>> offered = stillOffered(*doc);
        for (size_t i = 0; i < ALScriptNoticeBar::BUTTONS && i < offered.size(); ++i)
        {
            buttons[i] = offered[i];
        }
    }
    else if (doc && doc->compiledDiffers)
    {
        text       = services.words("NoticeCompiledDiffers");
        buttons[0] = { "compare_compiled", "NoticeCompare" };
        buttons[1] = { "keep_source", "NoticeKeepSource" };
        buttons[2] = { "take_compiled", "NoticeTakeCompiled" };
    }
    return notice;
}

void ALScriptStudioOrphans::refreshNotice()
{
    if (ALScriptNoticeBar* bar = mWindow.noticeBar())
    {
        bar->show(noticeFor(mServices.frontDoc(), mServices));
    }
}

void ALScriptStudioOrphans::noticeAction(const std::string& action)
{
    Doc* doc = mServices.frontDoc();
    if (!doc)
    {
        return;
    }
    if (offerSaid(*doc) && (action == "close" || doc->offer->offers(action)))
    {
        // The offer let go of, or taken up as its link in Output would be.
        doc->offer.reset();
        if (action != "close")
        {
            mWindow.takeOffer(*doc, action);
        }
        refreshNotice();
        return;
    }
    LLStringUtil::format_map_t args;
    args["[NAME]"] = doc->name;
    if (action == "close")
    {
        // Hidden until there is something else to say; a kept text from an
        // earlier session stays offered, under File > Recover Unsaved
        // Changes, once the notice is gone.
        doc->orphan->noticeDismissed = true;
        doc->recoverable.reset();
        // An offer of nothing but a copy or a file is what a tab gone or
        // out of reach says of itself: hidden with it, not said next.
        if (doc->offer && std::ranges::all_of(doc->offer->actions, [](const std::string& each) { return each == "copy" || each == "export"; }))
        {
            doc->offer.reset();
        }
    }
    else if (action == "restore" && doc->recoverable)
    {
        const ALRecoveryEntry entry = *doc->recoverable;
        doc->recoverable.reset();
        mWindow.endCompare(*doc);
        mRecovery.takeUp(*doc, entry);
        // A tab left holding it on its own has said why instead.
        if (doc->orphan->kind == Orphan::None)
        {
            mServices.report(mServices.words("RecoveryRestored", args), false, doc);
        }
    }
    else if (action == "compare_kept" && doc->recoverable)
    {
        // What the tab holds now beside what was kept; the notice stays,
        // to restore or let go of it after looking. What is offered is the
        // entry's listing, its text read as it is looked at, as a restore
        // reads it: where it cannot be, recovery says so.
        if (!mRecovery.wholeOf(*doc->recoverable))
        {
            return;
        }
        LLStringUtil::format_map_t when;
        when["[WHEN]"] = doc->recoverable->whenSaid();
        mWindow.compare(*doc, doc->editor->wholeText(), doc->recoverable->text, mServices.words("CompareNow"), mServices.words("CompareKept", when));
    }
    else if (action == "compare_compiled" && doc->compiledDiffers)
    {
        mWindow.compare(*doc, *doc->compiledDiffers, *doc->uploaded.text, mServices.words("CompareCompiled"), mServices.words("CompareMade"));
    }
    else if (action == "keep_source" && doc->compiledDiffers)
    {
        // The next save replaces the compiled half, as it would have.
        doc->compiledDiffers.reset();
        mWindow.endCompare(*doc);
    }
    else if (action == "take_compiled" && doc->compiledDiffers)
    {
        // What was compiled, the source now, as one step to undo; still
        // wrapped, so that Firestorm opens it as its source.
        doc->carriedText = *doc->compiledDiffers;
        doc->compiledDiffers.reset();
        mWindow.endCompare(*doc);
        mTabs.takeCarriedText(*doc);
        mServices.report(mServices.words("CompiledTaken", args), false, doc);
    }
    else if (action == "discard_left" && doc->recoverable)
    {
        mWindow.endCompare(*doc);
        mWindow.discardRecovery(*doc->recoverable);
        doc->recoverable.reset();
        mServices.report(mServices.words("RecoveryDiscarded", args), false, doc);
    }
    else if (action == "retry_load" && doc->orphan->detached && doc->loaded)
    {
        retryLoad(*doc);
    }
    else if (action == "copy")
    {
        mWindow.saveCopyToInventory(*doc);
    }
    else if (action == "export")
    {
        mWindow.saveCopyToFile();
    }
    else if (action == "save")
    {
        mSaves.saveAsked(*doc);
    }
    refreshNotice();
}
