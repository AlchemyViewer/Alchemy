/**
 * @file llpreviewnotecard.h
 * @brief LLPreviewNotecard class header file
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

#ifndef LL_LLPREVIEWNOTECARD_H
#define LL_LLPREVIEWNOTECARD_H

#include "alquickask.h"
#include "alrecoverykeeper.h"
#include "alsavehistory.h"
#include "alscripttypes.h"
#include "altextundo.h"
#include "llpreview.h"
#include "llvoinventorylistener.h"

#include <boost/signals2.hpp>

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class ALDiffView;
class ALNotecardEmbedded;
class ALTextView;
class LLButton;
class LLLayoutPanel;
class LLLineEditor;
class LLLiveLSLFile;
class LLTextBox;

// A notecard, for anyone to read and write, as the viewer has always opened
// one: from the inventory, an object's contents, an offer, a link. Its text
// is on the engine Script Studio's editors use (ALTextView) -- the reading
// face, wrapped, spell-checked, no line numbers -- with the items it
// carries as buttons in it (ALNotecardEmbedded), which open, copy out and
// drag out. It saves as every editor does (ALScriptWorkspace), hears a save
// of the same notecard made anywhere else, and keeps what is typed in the
// recovery store against the viewer closing before it was saved.
class LLPreviewNotecard final : public LLPreview, public LLVOInventoryListener
{
public:
    AL_VIEW_TYPE(LLPreviewNotecard, LLPreview);

    LLPreviewNotecard(const LLSD& key);
    ~LLPreviewNotecard() override;

    bool postBuild() override;
    void setObjectID(const LLUUID& object_id) override;
    void draw() override;
    // Its keys before the viewer's menu has them: Control-F is Search out
    // there, and Control-G Gestures.
    bool hasAccelerators() const override { return true; }
    bool handleKeyHere(KEY key, MASK mask) override;
    bool canClose() override;

    void inventoryChanged(LLViewerObject* object, LLInventoryObject::object_list_t* inventory, S32 serial_num, void* user_data) override;

    // A text the window kept, taken up where it belongs -- this window
    // for its notecard, opened, the text put in as it loads -- where the
    // notecard can be had: false where it cannot, and whoever asked keeps
    // it some other way.
    static bool recover(const ALRecoveryEntry& entry);

protected:
    void updateTitleButtons() override;
    void loadAsset() override;

private:
    ALScriptRef ref() const { return ALScriptRef(mObjectUUID, mItemUUID); }

    // --- loading ---------------------------------------------------------------------

    // The workspace's answer: the text and its items put in, or why there
    // are none said where the text would be.
    void loaded(const ALScriptLoaded& answer);
    // Nothing to show, and why: in place of the text, read-only.
    void showUnloaded(const std::string& why);
    // Whether the text may be changed, and the rest of the window with it:
    // the description, the lock, Edit and Save.
    void showModifiable(bool modifiable);

    // --- saving ------------------------------------------------------------------------

    bool saveIfNeeded();
    // A save of ours answered.
    void savedHere(const ALScriptCompileResult& result);
    // A save of this notecard heard from anywhere else: taken, marked
    // saved, or asked about (ALScriptSaved::heard).
    void heard(const ALScriptSaved& saved);
    void takeTheirs();
    void keepMine();
    // The text compared with what was saved elsewhere, or with a save of
    // its history; and back.
    void toggleCompare();
    bool handleSaveChangesDialog(const LLSD& notification, const LLSD& response);
    void deleteNotecard();
    bool handleConfirmDeleteDialog(const LLSD& notification, const LLSD& response);

    // Go to Line, as Script Studio's is, by the numbers a script reads a
    // notecard's lines by: from 0.
    void goToLine();

    // --- what it was saved as before ------------------------------------------------------

    // Its saves listed, as Script Studio's File > Local History lists a
    // tab's (ALScriptStudioHistory); one chosen compared with the text now,
    // and offered back as one step to undo; and let go of, with its notice.
    void showHistory();
    void compareSave(ALSavedText saved);
    // What the notice says of the save compared, and offers.
    void showHistoryNotice();
    void restoreSave();
    void endHistory();

    // --- what the window says ---------------------------------------------------------

    void setStatus(const std::string& text, bool failure = false);
    // A line over the text, with up to three buttons, each doing what it
    // is given; none hides it.
    struct NoticeButton
    {
        std::string           label;
        std::function<void()> action;
    };
    void showNotice(const std::string& text, std::vector<NoticeButton> buttons);
    void hideNotice();

    // --- kept against a crash -----------------------------------------------------------

    ALRecoveryEntry recoveryEntry() const;
    // What earlier sessions kept of this notecard, offered once it has
    // loaded; and one taken up over the text, as one step to undo, or with
    // its history where nothing was typed here.
    void offerKept();
    void takeUp(ALRecoveryEntry entry);

    // --- the external editor ------------------------------------------------------------

    void        openInExternalEditor();
    bool        onExternalChange(const std::string& filename);
    bool        loadNotecardText(const std::string& filename);
    bool        writeToFile(const std::string& filename);
    void        syncExternal();
    std::string getCleanNameForTmpFile() const;
    std::string getTmpFileName(const std::string& note_name) const;

    ALTextView*                          mText        = nullptr;
    LLLineEditor*                        mDescEditor  = nullptr;
    LLButton*                            mSaveBtn     = nullptr;
    LLButton*                            mEditBtn     = nullptr;
    LLButton*                            mDeleteBtn   = nullptr;
    LLButton*                            mHistoryBtn  = nullptr;
    LLUICtrl*                            mLockBtn     = nullptr;
    LLTextBox*                           mStatus      = nullptr;
    LLLayoutPanel*                       mNoticePanel = nullptr;
    LLTextBox*                           mNoticeText  = nullptr;
    std::array<LLButton*, 3>             mNoticeButtons{};
    std::array<std::function<void()>, 3> mNoticeActions;
    ALDiffView*                          mCompare     = nullptr;
    std::shared_ptr<ALNotecardEmbedded>  mItems;
    ALQuickAsk                           mQuickAsk;

    std::string mNoteName;
    LLUUID      mAssetID;
    bool        mLoaded     = false;
    bool        mModifiable = false;

    // A save of ours on its way: its request, where the text stood as it
    // went, and the items it carried.
    U64                   mSaveRequest = 0;
    bool                  mSaving      = false;
    ALTextUndo::SavePoint mSavePoint;
    std::vector<LLUUID>   mSentItems;
    // Saved elsewhere while this had changes of its own: that text, until
    // it is taken or kept.
    std::optional<std::string> mSavedThere;
    // A save of its history compared with it, until the comparison ends.
    std::optional<ALSavedText> mHistoryShown;
    // The notecard loaded again for a Take Theirs, whose text goes in over
    // what was typed rather than as a fresh load.
    bool                       mTakingTheirs = false;
    // Where the caret and the view were, for a load of a save heard from
    // elsewhere to put them back.
    std::optional<std::pair<ALTextPos, S32>> mKeepPlace;

    // What is typed kept in the recovery store; and a kept text to take
    // up once the notecard has loaded.
    ALRecoveryKeeper                     mKeeper;
    std::optional<ALRecoveryEntry>       mPendingRecovery;

    boost::signals2::scoped_connection mSavedConnection;
    boost::signals2::scoped_connection mFullConnection;

    LLLiveLSLFile* mLiveFile = nullptr;
};

#endif // LL_LLPREVIEWNOTECARD_H
