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

#include "alscriptrecovery.h"
#include "alscriptworkspace.h"
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
    bool handleKeyHere(KEY key, MASK mask) override;
    bool canClose() override;

    // Saved where it has changes: false where it could not be sent.
    bool saveItem();

    void inventoryChanged(LLViewerObject* object, LLInventoryObject::object_list_t* inventory, S32 serial_num, void* user_data) override;

    // The key the window keeps a notecard's unsaved text under in the
    // recovery store (ALScriptRecoveryStore::windowKeyOf).
    static std::string recoveryKeyOf(const LLUUID& object, const LLUUID& item);
    // A text the window kept, taken up where it belongs -- this window
    // for its notecard, opened, the text put in as it loads -- where the
    // notecard can be had: false where it cannot, and whoever asked keeps
    // it some other way.
    static bool recover(const ALScriptRecoveryEntry& entry);

protected:
    void updateTitleButtons() override;
    void loadAsset() override;

private:
    ALScriptRef ref() const { return ALScriptRef(mObjectUUID, mItemUUID); }

    // --- loading ---------------------------------------------------------------------

    // The workspace's answer: the text and its items put in, or why there
    // are none said where the text would be.
    void loaded(const ALScriptWorkspace::Loaded& answer);
    // Nothing to show, and why: in place of the text, read-only.
    void showUnloaded(const std::string& why);
    // Whether the text may be changed, and the rest of the window with it:
    // the description, the lock, Edit and Save.
    void showModifiable(bool modifiable);

    // --- saving ------------------------------------------------------------------------

    bool saveIfNeeded();
    // A save of ours answered.
    void savedHere(const ALScriptWorkspace::CompileResult& result);
    // A save of this notecard heard from anywhere else: taken, marked
    // saved, or asked about (ALScriptWorkspace::heard).
    void heard(const ALScriptWorkspace::Saved& saved);
    void takeTheirs();
    void keepMine();
    void toggleCompare();
    bool handleSaveChangesDialog(const LLSD& notification, const LLSD& response);
    void deleteNotecard();
    bool handleConfirmDeleteDialog(const LLSD& notification, const LLSD& response);

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

    ALScriptRecoveryEntry recoveryEntry() const;
    // What is unsaved written to the store a moment after the typing, or
    // this window's entry forgotten where nothing is unsaved.
    void keepSoon();
    void forgetKept();
    // What earlier sessions kept of this notecard, offered once it has
    // loaded; and one taken up over the text, as one step to undo, or with
    // its history where nothing was typed here.
    void offerKept();
    void takeUp(ALScriptRecoveryEntry entry);

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
    LLUICtrl*                            mLockBtn     = nullptr;
    LLTextBox*                           mStatus      = nullptr;
    LLLayoutPanel*                       mNoticePanel = nullptr;
    LLTextBox*                           mNoticeText  = nullptr;
    std::array<LLButton*, 3>             mNoticeButtons{};
    std::array<std::function<void()>, 3> mNoticeActions;
    ALDiffView*                          mCompare     = nullptr;
    std::shared_ptr<ALNotecardEmbedded>  mItems;

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
    // The notecard loaded again for a Take Theirs, whose text goes in over
    // what was typed rather than as a fresh load.
    bool                       mTakingTheirs = false;
    // Where the caret and the view were, for a load of a save heard from
    // elsewhere to put them back.
    std::optional<std::pair<ALTextPos, S32>> mKeepPlace;

    // The recovery store's key for this notecard; when what is typed is
    // next written; whether an entry of this session's is there to
    // forget; a failure to write it said once; and a kept text to take
    // up once the notecard has loaded.
    std::string                          mRecoveryKey;
    F64                                  mRecoveryDue     = 0.0;
    bool                                 mRecoveryWritten = false;
    bool                                 mRecoveryFailed  = false;
    std::optional<ALScriptRecoveryEntry> mPendingRecovery;

    boost::signals2::scoped_connection mSavedConnection;
    boost::signals2::scoped_connection mChangedConnection;
    boost::signals2::scoped_connection mFullConnection;

    LLLiveLSLFile* mLiveFile = nullptr;
};

#endif // LL_LLPREVIEWNOTECARD_H
