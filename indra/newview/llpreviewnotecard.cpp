/**
 * @file llpreviewnotecard.cpp
 * @brief Implementation of the notecard editor
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llpreviewnotecard.h"

#include "aldiffview.h"
#include "alnotecardembedded.h"
#include "alsaid.h"
#include "alscriptstudiorecovery.h"
#include "alsurface.h"
#include "altextview.h"

#include "llagent.h"
#include "llappviewer.h"
#include "llbutton.h"
#include "lldraghandle.h"
#include "llexternaleditor.h"
#include "llfloaterreg.h"
#include "llinventorymodel.h"
#include "lllayoutstack.h"
#include "lllineeditor.h"
#include "llmd5.h"
#include "llnotecard.h"
#include "llnotificationsutil.h"
#include "llpreviewscript.h"
#include "roles_constants.h"
#include "lltextbox.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "llviewerinventory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"

#include <algorithm>
#include <set>

namespace
{
    // How long after typing stops, or after the first change of a run of
    // typing, what is unsaved is written: as Script Studio writes its tabs.
    constexpr F64 RECOVERY_DELAY = 1.5;
}

///----------------------------------------------------------------------------
/// Class LLPreviewNotecard
///----------------------------------------------------------------------------

LLPreviewNotecard::LLPreviewNotecard(const LLSD& key) : LLPreview(key)
{
    const LLInventoryItem* item = getItem();
    mNoteName                   = "New Note";
    if (item)
    {
        mAssetID = item->getAssetUUID();
        if (!item->getName().empty())
        {
            mNoteName = item->getName();
        }
    }
}

LLPreviewNotecard::~LLPreviewNotecard()
{
    delete mLiveFile;
}

bool LLPreviewNotecard::postBuild()
{
    mText = getChild<ALTextView>("notecard_text");
    mText->setPlaceholder(getString("Loading"));
    mText->setReadOnly(true);
    mChangedConnection = mText->onTextChanged([this]() { keepSoon(); });
    mFullConnection    = mText->onFull([this]() {
        LLStringUtil::format_map_t args;
        args["[NAME]"]  = mNoteName;
        args["[LIMIT]"] = std::to_string(static_cast<S32>(LLNotecard::MAX_SIZE));
        setStatus(alSaid("NotecardFull", "[NAME] is full: a notecard holds at most [LIMIT] bytes", args), true);
    });
    // A save of this notecard made anywhere: in Script Studio, from VS
    // Code, by a copy of the viewer's own editors.
    mSavedConnection = ALScriptWorkspace::instance().onSaved([this](const ALScriptWorkspace::Saved& saved) { heard(saved); });

    mSaveBtn = getChild<LLButton>("Save");
    mSaveBtn->setCommitCallback([this](LLUICtrl*, const LLSD&) { saveIfNeeded(); });

    mLockBtn = getChild<LLUICtrl>("lock");
    mLockBtn->setVisible(false);

    mDeleteBtn = getChild<LLButton>("Delete");
    mDeleteBtn->setCommitCallback([this](LLUICtrl*, const LLSD&) { deleteNotecard(); });
    mDeleteBtn->setEnabled(false);

    mEditBtn = getChild<LLButton>("Edit");
    mEditBtn->setCommitCallback([this](LLUICtrl*, const LLSD&) { openInExternalEditor(); });

    mStatus      = getChild<LLTextBox>("status");
    mNoticePanel = getChild<LLLayoutPanel>("notice_panel");
    mNoticeText  = getChild<LLTextBox>("notice_text");
    for (size_t i = 0; i < mNoticeButtons.size(); ++i)
    {
        mNoticeButtons[i] = getChild<LLButton>("notice_" + std::to_string(i + 1));
        mNoticeButtons[i]->setCommitCallback([this, i](LLUICtrl*, const LLSD&) {
            // The action may show another notice, which takes its place.
            const std::function<void()> action = mNoticeActions[i];
            if (action)
            {
                action();
            }
        });
    }

    const LLInventoryItem* item = getItem();
    mDescEditor                 = getChild<LLLineEditor>("desc");
    mDescEditor->setCommitCallback(boost::bind(&LLPreview::onText, mDescEditor, this));
    if (item)
    {
        if (!item->getName().empty())
        {
            mNoteName = item->getName();
        }
        mDescEditor->setValue(item->getDescription());
        const bool source_library = mObjectUUID.isNull() && gInventory.isObjectDescendentOf(item->getUUID(), gInventory.getLibraryRootFolderID());
        mDeleteBtn->setEnabled(!source_library);
    }
    mDescEditor->setPrevalidate(&LLTextValidate::validateASCIIPrintableNoPipe);

    return LLPreview::postBuild();
}

bool LLPreviewNotecard::saveItem()
{
    return saveIfNeeded();
}

void LLPreviewNotecard::draw()
{
    mSaveBtn->setEnabled(mLoaded && mModifiable && !mSaving && mText->isDirty());
    // What was typed a moment ago written, and a write that failed said
    // once.
    if (mRecoveryDue > 0.0 && LLTimer::getTotalSeconds() >= mRecoveryDue)
    {
        mRecoveryDue = 0.0;
        if (ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store(); kept && mText->isDirty())
        {
            kept->writeSoon(recoveryEntry());
            mRecoveryWritten = true;
        }
    }
    if (mRecoveryWritten)
    {
        if (ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store())
        {
            for (const std::string& key : kept->takeFailures())
            {
                if (key == mRecoveryKey && !mRecoveryFailed)
                {
                    mRecoveryFailed = true;
                    setStatus(getString("RecoveryWriteFailed"), true);
                }
            }
        }
    }
    LLPreview::draw();
}

// virtual
bool LLPreviewNotecard::handleKeyHere(KEY key, MASK mask)
{
    if (('S' == key) && (MASK_CONTROL == (mask & MASK_CONTROL)))
    {
        saveIfNeeded();
        return true;
    }
    // Find, from anywhere in the window: the text's own bar.
    if (('F' == key) && (MASK_CONTROL == (mask & MASK_CONTROL)))
    {
        mText->setFocus(true);
        mText->perform(ALEditorCommand::Find);
        return true;
    }
    return LLPreview::handleKeyHere(key, mask);
}

// virtual
bool LLPreviewNotecard::canClose()
{
    if (mForceClose || !mText || !mLoaded || !mText->isDirty())
    {
        return true;
    }
    if (!mSaveDialogShown)
    {
        mSaveDialogShown = true;
        // Bring up view-modal dialog: Save changes? Yes, No, Cancel
        LLNotificationsUtil::add("SaveChanges", LLSD(), LLSD(), boost::bind(&LLPreviewNotecard::handleSaveChangesDialog, this, _1, _2));
    }
    return false;
}

/* virtual */
void LLPreviewNotecard::setObjectID(const LLUUID& object_id)
{
    LLPreview::setObjectID(object_id);
}

void LLPreviewNotecard::updateTitleButtons()
{
    LLPreview::updateTitleButtons();

    if (mLockBtn && mLockBtn->getVisible() && !isMinimized()) // lock button stays visible if floater is minimized.
    {
        LLRect lock_rc      = mLockBtn->getRect();
        LLRect buttons_rect = getDragHandle()->getButtonsRect();
        buttons_rect.mLeft  = lock_rc.mLeft;
        getDragHandle()->setButtonsRect(buttons_rect);
    }
}

// --- loading ---------------------------------------------------------------------------------

void LLPreviewNotecard::loadAsset()
{
    const LLInventoryItem* item = getItem();
    if (!item)
    {
        if (mObjectUUID.notNull() && mItemUUID.notNull())
        {
            LLViewerObject* object = gObjectList.findObject(mObjectUUID);
            if (object && (object->isInventoryPending() || object->isInventoryDirty()))
            {
                // A notecard in an object whose contents are not here yet:
                // loaded once they are (inventoryChanged).
                registerVOInventoryListener(object, nullptr);
                if (object->isInventoryDirty())
                {
                    object->requestInventory();
                }
                return;
            }
            if (object)
            {
                LLStringUtil::format_map_t args;
                args["[REASON]"] = LLTrans::getString("WorkspaceNoSuchItemInObject");
                showUnloaded(getString("ReadOnlyFailed", args));
            }
            else
            {
                showUnloaded(getString("no_object"));
            }
            mAssetStatus = PREVIEW_ASSET_ERROR;
        }
        // Otherwise the item is not known yet -- an object's notecard
        // before its object is set -- and this is asked again then.
        return;
    }
    if (!item->getName().empty())
    {
        mNoteName = item->getName();
    }
    mRecoveryKey = recoveryKeyOf(mObjectUUID, mItemUUID);

    const LLPermissions& perm           = item->getPermissions();
    const bool           is_owner       = gAgent.allowOperation(PERM_OWNER, perm, GP_OBJECT_MANIPULATE);
    const bool           allow_modify   = canModify(mObjectUUID, item);
    const bool           source_library = mObjectUUID.isNull() && gInventory.isObjectDescendentOf(mItemUUID, gInventory.getLibraryRootFolderID());
    mDeleteBtn->setEnabled((allow_modify || is_owner) && !source_library);

    mAssetStatus = PREVIEW_ASSET_LOADING;
    if (item->getAssetUUID().isNull())
    {
        // Made and never saved: nothing to fetch.
        ALScriptWorkspace::Loaded answer;
        answer.ref        = ref();
        answer.name       = mNoteName;
        answer.viewable   = true;
        answer.modifiable = allow_modify;
        answer.notecard   = true;
        loaded(answer);
        return;
    }
    // Through the workspace, as every editor loads: copy is enough to
    // read, modify decides whether it can be changed.
    LLHandle<LLFloater> handle = getHandle();
    ALScriptWorkspace::instance().load(ref(), [handle](const ALScriptWorkspace::Loaded& answer) {
        if (LLPreviewNotecard* self = ALViewType::as<LLPreviewNotecard>(handle.get()))
        {
            self->loaded(answer);
        }
    });
}

void LLPreviewNotecard::loaded(const ALScriptWorkspace::Loaded& answer)
{
    if (answer.ref.item != mItemUUID || answer.ref.object != mObjectUUID)
    {
        // Loaded for the notecard this was before a save made it another
        // item; that one is loaded again.
        return;
    }
    if (!answer.error.empty())
    {
        mTakingTheirs = false;
        mKeepPlace.reset();
        LLStringUtil::format_map_t args;
        args["[REASON]"] = answer.error;
        if (mLoaded)
        {
            // Loaded again, for a save heard from elsewhere, and failed:
            // what is here stays.
            setStatus(getString("ReadOnlyFailed", args), true);
            return;
        }
        showUnloaded(getString(answer.failure == ALScriptWorkspace::Loaded::Failure::NotPermitted ? "not_allowed" : "ReadOnlyFailed", args));
        mAssetStatus = PREVIEW_ASSET_ERROR;
        return;
    }
    const bool taking = mTakingTheirs;
    mTakingTheirs     = false;
    mAssetID          = answer.assetId;
    mLoaded           = true;
    mModifiable       = answer.modifiable && canModify(mObjectUUID, getItem());
    mSavedThere.reset();
    if (!mItems)
    {
        ALNotecardEmbedded::Holder holder;
        holder.notecard   = [this]() { return ref(); };
        holder.changeable = [this]() { return mLoaded && mModifiable; };
        holder.say        = [this](const std::string& words, bool error) { setStatus(words, error); };
        mItems            = std::make_shared<ALNotecardEmbedded>(*mText, std::move(holder), ALNotecardEmbedded::viewer());
        mItems->wire();
    }
    mItems->loaded(answer.embedded);
    if (taking && mText->isDirty())
    {
        // Theirs over what was typed here, as one step: Undo brings back
        // what was typed, which the store keeps too.
        if (ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store())
        {
            kept->setAside(recoveryEntry());
        }
        const ALTextDocument& text = mText->document();
        mText->setReadOnly(false);
        mText->replaceAll({ { ALTextRange(text.start(), text.end()), answer.text } });
        mText->resetDirty();
    }
    else
    {
        mText->setText(answer.text);
    }
    if (mKeepPlace)
    {
        mText->setCaret(mText->document().clamp(mKeepPlace->first));
        mText->setScrollY(mKeepPlace->second);
        mKeepPlace.reset();
    }
    mText->setPlaceholder(std::string());
    mItems->place();
    // No more than a notecard is read back with.
    mText->setMaxBytes(LLNotecard::MAX_SIZE);
    showModifiable(mModifiable);
    hideNotice();
    if (mCompare && mCompare->getVisible())
    {
        toggleCompare();
    }
    mAssetStatus = PREVIEW_ASSET_LOADED;
    forgetKept();
    syncExternal();
    if (mPendingRecovery)
    {
        ALScriptRecoveryEntry entry = std::move(*mPendingRecovery);
        mPendingRecovery.reset();
        takeUp(std::move(entry));
    }
    else
    {
        offerKept();
    }
}

void LLPreviewNotecard::showUnloaded(const std::string& why)
{
    mLoaded = false;
    mText->setText(std::string());
    mText->setPlaceholder(why);
    showModifiable(false);
}

void LLPreviewNotecard::showModifiable(bool modifiable)
{
    mText->setReadOnly(!modifiable);
    mLockBtn->setVisible(!modifiable);
    mDescEditor->setEnabled(modifiable);
    mEditBtn->setEnabled(modifiable);
    // Read-only, and why, while nothing else is said.
    if (mLoaded && !modifiable)
    {
        setStatus(getString("ReadOnlyNoModify"));
    }
    updateTitleButtons();
}

/*virtual*/
void LLPreviewNotecard::inventoryChanged(LLViewerObject* object, LLInventoryObject::object_list_t* inventory, S32 serial_num, void* user_data)
{
    removeVOInventoryListener();
    loadAsset();
}

// --- saving -----------------------------------------------------------------------------------

bool LLPreviewNotecard::saveIfNeeded()
{
    if (!mText->isDirty())
    {
        if (mCloseAfterSave)
        {
            closeFloater();
        }
        return true;
    }
    if (!mLoaded || !mModifiable)
    {
        return false;
    }
    // The text with each item it still stands, numbered afresh; the
    // editor's own left as they are.
    std::string                  text;
    ALNotecardEmbedded::items_t  items;
    mItems->forSave(text, items);
    mSentItems.clear();
    for (const LLPointer<LLInventoryItem>& item : items)
    {
        mSentItems.push_back(item->getUUID());
    }
    ALScriptWorkspace& workspace = ALScriptWorkspace::instance();
    mSaveRequest                 = workspace.newRequest();
    mSavePoint                   = mText->savePoint();
    LLHandle<LLFloater> handle   = getHandle();
    std::string         error;
    const bool          sent = workspace.saveNotecard(
        ref(), text, items,
        [handle](const ALScriptWorkspace::CompileResult& result) {
            if (LLPreviewNotecard* self = ALViewType::as<LLPreviewNotecard>(handle.get()))
            {
                self->savedHere(result);
            }
        },
        error, ALScriptWorkspace::Sender(ALScriptWorkspace::Origin::Editor, mSaveRequest));
    if (!sent)
    {
        LLStringUtil::format_map_t args;
        args["[REASON]"] = error;
        setStatus(getString("SaveFailed", args), true);
        mCloseAfterSave = false;
        return false;
    }
    mSaving = true;
    setStatus(getString("Saving"));
    return true;
}

void LLPreviewNotecard::savedHere(const ALScriptWorkspace::CompileResult& result)
{
    if (result.sender.request != mSaveRequest)
    {
        return;
    }
    mSaving = false;
    if (!result.error.empty())
    {
        LLStringUtil::format_map_t args;
        args["[REASON]"] = result.error;
        setStatus(getString("SaveFailed", args), true);
        // Not closed after all: a quit that waited on it waits no more.
        if (mCloseAfterSave)
        {
            mCloseAfterSave = false;
            LLAppViewer::instance()->abortQuit();
        }
        return;
    }
    mText->markSavedAt(mSavePoint);
    mItems->saved(mSentItems);
    if (result.newAssetId.notNull())
    {
        mAssetID = result.newAssetId;
    }
    // Saved as another item -- one the agent could not change in place:
    // this window is that item's now.
    if (result.newItemId.notNull() && result.newItemId != mItemUUID)
    {
        mItemUUID    = result.newItemId;
        mRecoveryKey = recoveryKeyOf(mObjectUUID, mItemUUID);
        setKey(LLSD(mItemUUID));
    }
    setStatus(getString("Saved"));
    keepSoon();
    if (mCloseAfterSave)
    {
        closeFloater();
    }
}

void LLPreviewNotecard::heard(const ALScriptWorkspace::Saved& saved)
{
    if (!mLoaded || saved.kind != ALScriptWorkspace::Kind::Notecard || saved.ref.item != mItemUUID || saved.ref.object != mObjectUUID)
    {
        return;
    }
    // Our own save lands as its answer; one of ours on its way lands after
    // this, and is what the server keeps.
    if ((saved.sender.origin == ALScriptWorkspace::Origin::Editor && saved.sender.request == mSaveRequest) || mSaving)
    {
        return;
    }
    if (saved.asset.notNull())
    {
        mAssetID = saved.asset;
    }
    switch (ALScriptWorkspace::heard(saved.text, mText->wholeText(), mText->isDirty() && mModifiable,
                                     [this]() { return mText->undoJournal().savedText(); }))
    {
        case ALScriptWorkspace::Heard::Same:
            mText->resetDirty();
            forgetKept();
            return;
        case ALScriptWorkspace::Heard::Keep:
            return;
        case ALScriptWorkspace::Heard::Ask:
        {
            mSavedThere = saved.text;
            LLStringUtil::format_map_t args;
            args["[NAME]"] = mNoteName;
            args["[WHO]"]  = getString(saved.sender.origin == ALScriptWorkspace::Origin::Bridge   ? "SavedByBridge"
                                       : saved.sender.origin == ALScriptWorkspace::Origin::Studio ? "SavedByStudio"
                                                                                                  : "SavedByOther");
            showNotice(getString("SavedElsewhere", args), { { getString("TakeTheirs"), [this]() { takeTheirs(); } },
                                                            { getString("KeepMine"), [this]() { keepMine(); } },
                                                            { getString("Compare"), [this]() { toggleCompare(); } } });
            return;
        }
        case ALScriptWorkspace::Heard::Take:
            // Nothing typed here: loaded again, for its items as well as
            // its text, the caret and the view kept where they were.
            mKeepPlace = std::make_pair(mText->caret(), mText->scrollY());
            loadAsset();
            return;
    }
}

void LLPreviewNotecard::takeTheirs()
{
    if (!mSavedThere)
    {
        return;
    }
    // Loaded again for its items; the text goes in over what was typed,
    // as one step.
    mTakingTheirs = true;
    hideNotice();
    loadAsset();
}

void LLPreviewNotecard::keepMine()
{
    mSavedThere.reset();
    hideNotice();
    if (mCompare && mCompare->getVisible())
    {
        toggleCompare();
    }
    setStatus(getString("KeptMine"));
}

void LLPreviewNotecard::toggleCompare()
{
    LLView* host = mText->getParent();
    if (!mCompare)
    {
        ALDiffView::Params p(LLUICtrlFactory::getDefaultParams<ALDiffView>());
        p.name = "compare";
        p.rect = mText->getRect();
        p.follows.flags(FOLLOWS_ALL);
        mCompare = LLUICtrlFactory::create<ALDiffView>(p);
        mCompare->setVisible(false);
        mCompare->setFont(mText->getFont());
        mCompare->setOnEscape([this]() { toggleCompare(); });
        host->addChild(mCompare);
    }
    const bool comparing = !mCompare->getVisible() && mSavedThere;
    if (comparing)
    {
        mCompare->setRect(mText->getRect());
        mCompare->setTexts(*mSavedThere, mText->wholeText());
        mCompare->setTitles(getString("CompareTheirs"), getString("CompareMine"));
    }
    mCompare->setVisible(comparing);
    mText->setVisible(!comparing);
    (comparing ? static_cast<LLView*>(mCompare) : static_cast<LLView*>(mText))->setFocus(true);
    if (mNoticePanel->getVisible())
    {
        mNoticeButtons[2]->setLabel(getString(comparing ? "BackToText" : "Compare"));
    }
}

bool LLPreviewNotecard::handleSaveChangesDialog(const LLSD& notification, const LLSD& response)
{
    mSaveDialogShown = false;
    S32 option       = LLNotificationsUtil::getSelectedOption(notification, response);
    switch (option)
    {
        case 0: // "Yes"
            mCloseAfterSave = true;
            if (!saveIfNeeded())
            {
                // Not sent, which the window says; nothing closes.
                LLAppViewer::instance()->abortQuit();
            }
            break;

        case 1: // "No"
            // Thrown away, and set aside a while all the same, in case
            // that was a mistake.
            if (ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store())
            {
                kept->setAside(recoveryEntry());
            }
            forgetKept();
            mForceClose = true;
            closeFloater();
            break;

        case 2: // "Cancel"
        default:
            // If we were quitting, we didn't really mean it.
            LLAppViewer::instance()->abortQuit();
            break;
    }
    return false;
}

void LLPreviewNotecard::deleteNotecard()
{
    LLNotificationsUtil::add("DeleteNotecard", LLSD(), LLSD(), boost::bind(&LLPreviewNotecard::handleConfirmDeleteDialog, this, _1, _2));
}

bool LLPreviewNotecard::handleConfirmDeleteDialog(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (option != 0)
    {
        // canceled
        return false;
    }

    if (mObjectUUID.isNull())
    {
        // move item from agent's inventory into trash
        LLViewerInventoryItem* item = gInventory.getItem(mItemUUID);
        if (item != NULL)
        {
            const LLUUID trash_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
            gInventory.changeItemParent(item, trash_id, false);
        }
    }
    else
    {
        // delete item from inventory of in-world object
        LLViewerObject* object = gObjectList.findObject(mObjectUUID);
        if (object)
        {
            LLViewerInventoryItem* item = dynamic_cast<LLViewerInventoryItem*>(object->getInventoryObject(mItemUUID));
            if (item != NULL)
            {
                object->removeInventory(mItemUUID);
            }
        }
    }

    // close floater, ignore unsaved changes
    forgetKept();
    mForceClose = true;
    closeFloater();
    return false;
}

// --- what the window says ---------------------------------------------------------------------

void LLPreviewNotecard::setStatus(const std::string& text, bool failure)
{
    static const LLUIColor alarm = LLUIColorTable::instance().getColor("LtOrange", LLColor4::yellow);
    mStatus->setColor(failure ? alarm : ALSurface::text());
    mStatus->setText(text);
    mStatus->setToolTip(text);
}

void LLPreviewNotecard::showNotice(const std::string& text, std::vector<NoticeButton> buttons)
{
    mNoticeText->setText(text);
    mNoticeText->setToolTip(text);
    for (size_t i = 0; i < mNoticeButtons.size(); ++i)
    {
        const bool shown = i < buttons.size();
        mNoticeButtons[i]->setVisible(shown);
        mNoticeActions[i] = shown ? buttons[i].action : std::function<void()>();
        if (shown)
        {
            mNoticeButtons[i]->setLabel(buttons[i].label);
        }
    }
    mNoticePanel->setVisible(true);
}

void LLPreviewNotecard::hideNotice()
{
    mNoticePanel->setVisible(false);
    mNoticeActions.fill(std::function<void()>());
}

// --- kept against a crash ---------------------------------------------------------------------

// static
std::string LLPreviewNotecard::recoveryKeyOf(const LLUUID& object, const LLUUID& item)
{
    return ALScriptRecoveryStore::windowKeyOf(object, item);
}

ALScriptRecoveryEntry LLPreviewNotecard::recoveryEntry() const
{
    ALScriptRecoveryEntry entry;
    entry.key       = mRecoveryKey;
    entry.object    = mObjectUUID;
    entry.item      = mItemUUID;
    entry.name      = mNoteName;
    entry.notecard  = true;
    entry.baseAsset = mAssetID;
    entry.text      = mText->text();
    if (mObjectUUID.notNull())
    {
        if (LLViewerObject* object = gObjectList.findObject(mObjectUUID); object && object->getRegion())
        {
            entry.region = object->getRegion()->getName();
        }
    }
    if (mItems)
    {
        entry.embedded = ALNotecardEmbedded::asLLSD(mItems->items());
    }
    entry.historyWritten = mText->undoJournal().asNotation();
    entry.caretLine      = mText->caret().line;
    entry.caretColumn    = mText->caret().column;
    return entry;
}

void LLPreviewNotecard::keepSoon()
{
    if (mRecoveryKey.empty() || !mLoaded || !mModifiable)
    {
        return;
    }
    if (!mText->isDirty())
    {
        forgetKept();
        return;
    }
    if (mRecoveryDue <= 0.0)
    {
        mRecoveryDue = LLTimer::getTotalSeconds() + RECOVERY_DELAY;
    }
}

void LLPreviewNotecard::forgetKept()
{
    mRecoveryDue = 0.0;
    if (!mRecoveryWritten || mRecoveryKey.empty())
    {
        return;
    }
    if (ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store())
    {
        kept->forget(mRecoveryKey);
    }
    mRecoveryWritten = false;
    mRecoveryFailed  = false;
}

void LLPreviewNotecard::offerKept()
{
    ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store();
    if (!kept || mRecoveryKey.empty() || !mModifiable)
    {
        return;
    }
    const std::optional<ALScriptRecoveryEntry> left = kept->leftFor(mRecoveryKey);
    if (!left)
    {
        return;
    }
    LLStringUtil::format_map_t args;
    args["[WHEN]"]                    = left->whenSaid();
    const ALScriptRecoveryEntry entry = *left;
    showNotice(getString("Recovered", args), { { getString("Restore"), [this, entry]() { takeUp(entry); } },
                                               { getString("Discard"), [this, entry]() {
                                                    if (ALScriptRecoveryStore* store = ALScriptStudioRecovery::store())
                                                    {
                                                        store->discard(entry);
                                                    }
                                                    hideNotice();
                                                } } });
}

void LLPreviewNotecard::takeUp(ALScriptRecoveryEntry entry)
{
    ALScriptRecoveryStore* kept = ALScriptStudioRecovery::store();
    hideNotice();
    if (!kept || (!entry.whole && !kept->load(entry)) || !mLoaded || !mModifiable)
    {
        return;
    }
    // Its items first: the text says them by their places in the list it
    // was kept with.
    mItems->take(ALNotecardEmbedded::fromLLSD(entry.embedded));
    // Nothing typed here: the text with the steps that led to it. Else, or
    // where the steps are not of it, the text over what is here, as one
    // step to undo.
    if (mText->isDirty() || !mText->setTextWithHistory(entry.text, entry.historyOf()))
    {
        const ALTextDocument& text = mText->document();
        mText->replaceAll({ { ALTextRange(text.start(), text.end()), entry.text } });
    }
    mItems->place();
    if (entry.caretLine >= 0)
    {
        mText->goTo(ALTextPos(entry.caretLine, entry.caretColumn));
    }
    // Kept as this window's own from here; the entry it came from goes
    // once that is written.
    if (!mText->isDirty())
    {
        mText->markUnsaved();
    }
    kept->write(recoveryEntry());
    mRecoveryWritten = true;
    kept->remove(entry);
    setStatus(getString("Restored"));
}

// static
bool LLPreviewNotecard::recover(const ALScriptRecoveryEntry& entry)
{
    // The notecard as the window opens it: in the inventory, or in an
    // object in sight whose contents say it is there.
    LLSD key;
    if (entry.object.isNull())
    {
        if (!gInventory.getItem(entry.item))
        {
            return false;
        }
        key = LLSD(entry.item);
    }
    else
    {
        LLViewerObject* object = gObjectList.findObject(entry.object);
        if (!object || !object->getInventoryItem(entry.item))
        {
            return false;
        }
        key["taskid"] = entry.object;
        key["itemid"] = entry.item;
    }
    LLPreviewNotecard* preview = LLFloaterReg::showTypedInstance<LLPreviewNotecard>("preview_notecard", key, TAKE_FOCUS_YES);
    if (!preview)
    {
        return false;
    }
    if (preview->mLoaded)
    {
        preview->takeUp(entry);
        return true;
    }
    // Taken up once it has loaded.
    preview->mPendingRecovery = entry;
    if (entry.object.notNull())
    {
        preview->setObjectID(entry.object);
    }
    return true;
}

// --- the external editor -----------------------------------------------------------------------

void LLPreviewNotecard::syncExternal()
{
    // Sync with external editor.
    std::string note_name = getCleanNameForTmpFile();
    std::string tmp_file  = getTmpFileName(note_name);
    llstat      s;
    if (LLFile::stat(tmp_file, &s) != 0)
    {
        // file doesn't exist, try with empty name
        note_name.clear();
        tmp_file = getTmpFileName(note_name);
        if (LLFile::stat(tmp_file, &s) != 0)
        {
            // file doesn't exist, with either name, give up
            return;
        }
    }

    if (mLiveFile)
    {
        mLiveFile->ignoreNextUpdate();
    }
    writeToFile(tmp_file);
}

void LLPreviewNotecard::openInExternalEditor()
{
    delete mLiveFile; // deletes file
    mLiveFile = nullptr;

    // Save the notecard to a temporary file.
    std::string note_name = getCleanNameForTmpFile();
    std::string filename  = getTmpFileName(note_name);
    if (!writeToFile(filename))
    {
        // In case some characters from notecard name are forbidden
        // and not accounted for, name is too long or some other issue,
        // try file that doesn't include notecard name
        note_name.clear();
        filename = getTmpFileName(note_name);
        writeToFile(filename);
    }

    // Start watching file changes.
    mLiveFile = new LLLiveLSLFile(filename, boost::bind(&LLPreviewNotecard::onExternalChange, this, _1));
    mLiveFile->ignoreNextUpdate();
    mLiveFile->addToEventTimer();

    // Open it in external editor.
    {
        LLExternalEditor             ed;
        LLExternalEditor::EErrorCode status;
        std::string                  msg;

        status = ed.setCommand("LL_SCRIPT_EDITOR");
        if (status != LLExternalEditor::EC_SUCCESS)
        {
            if (status == LLExternalEditor::EC_NOT_SPECIFIED) // Use custom message for this error.
            {
                msg = LLTrans::getString("ExternalEditorNotSet");
            }
            else
            {
                msg = LLExternalEditor::getErrorMessage(status);
            }

            LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", msg));
            return;
        }

        status = ed.run(filename);
        if (status != LLExternalEditor::EC_SUCCESS)
        {
            msg = LLExternalEditor::getErrorMessage(status);
            LLNotificationsUtil::add("GenericAlert", LLSD().with("MESSAGE", msg));
        }
    }
}

bool LLPreviewNotecard::onExternalChange(const std::string& filename)
{
    if (!loadNotecardText(filename))
    {
        return false;
    }
    saveIfNeeded();
    return true;
}

bool LLPreviewNotecard::loadNotecardText(const std::string& filename)
{
    if (filename.empty())
    {
        LL_WARNS() << "Empty file name" << LL_ENDL;
        return false;
    }
    if (!mLoaded || !mModifiable)
    {
        return false;
    }

    LLFILE* file = LLFile::fopen(filename, LLFILE_MODE("rb")); /*Flawfinder: ignore*/
    if (!file)
    {
        LL_WARNS() << "Error opening " << filename << LL_ENDL;
        return false;
    }

    // read in the whole file
    fseek(file, 0L, SEEK_END);
    size_t file_length = (size_t)ftell(file);
    fseek(file, 0L, SEEK_SET);
    std::string text(file_length, '\0');
    size_t      nread = fread(text.data(), 1, file_length, file);
    if (nread < file_length)
    {
        LL_WARNS() << "Short read" << LL_ENDL;
    }
    text.resize(nread);
    fclose(file);

    // What the other editor wrote, as one step to undo.
    const ALTextDocument& document = mText->document();
    mText->replaceAll({ { ALTextRange(document.start(), document.end()), text } });
    return true;
}

bool LLPreviewNotecard::writeToFile(const std::string& filename)
{
    LLFILE* fp = LLFile::fopen(filename, LLFILE_MODE("wb"));
    if (!fp)
    {
        LL_WARNS() << "Unable to write to " << filename << LL_ENDL;
        return false;
    }

    std::string utf8text = mText->text();

    if (utf8text.size() == 0)
    {
        utf8text = " ";
    }

    fputs(utf8text.c_str(), fp);
    fclose(fp);
    return true;
}

std::string LLPreviewNotecard::getCleanNameForTmpFile() const
{
    std::string note_name = mNoteName;
    if (note_name.empty())
    {
        note_name = "New note";
    }
    static const std::set<char> forbidden_chars{ '<', '>', ':', '"', '\\', '/', '|', '?', '*' };
    note_name.erase(std::remove_if(note_name.begin(), note_name.end(), [](char c) { return forbidden_chars.contains(c); }), note_name.end());
    return note_name;
}

std::string LLPreviewNotecard::getTmpFileName(const std::string& note_name) const
{
    std::string notecard_id = mObjectUUID.asString() + "_" + mItemUUID.asString();

    // Use MD5 sum to make the file name shorter and not exceed maximum path length.
    char  notecard_id_hash_str[33]; /* Flawfinder: ignore */
    LLMD5 notecard_id_hash((const U8*)notecard_id.c_str());
    notecard_id_hash.hex_digest(notecard_id_hash_str);

    if (note_name.empty())
    {
        return std::string(LLFile::tmpdir()) + "sl_notecard_" + notecard_id_hash_str + ".txt";
    }
    else
    {
        return std::string(LLFile::tmpdir()) + "sl_notecard_" + note_name + "_" + notecard_id_hash_str + ".txt";
    }
}

// EOF
