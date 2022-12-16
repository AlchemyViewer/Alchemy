/**
* @file llfloatertexturepicker.h
* @author Richard Nelson, James Cook
* @brief LLTextureCtrl class header file including related functions
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

#ifndef LL_FLOATERTEXTUREPICKER_H
#define LL_FLOATERTEXTUREPICKER_H

#include "llviewertexture.h"
#include "lltexturectrl.h"
#include "llfloater.h"

class LLFilterEditor;
class LLTabContainer;

typedef std::function<void(LLTextureCtrl::ETexturePickOp op, LLUUID id)> floater_commit_callback;
typedef std::function<void()> floater_close_callback;
typedef std::function<void(const LLUUID& asset_id)> set_image_asset_id_callback;
typedef std::function<void(LLPointer<LLViewerTexture> texture)> set_on_update_image_stats_callback;

class LLFloaterTexturePicker final : public LLFloater
{
  public:
    LLFloaterTexturePicker(LLView*            owner,
                           LLUUID             image_asset_id,
                           LLUUID             default_image_asset_id,
                           LLUUID             transparent_image_asset_id,
                           LLUUID             blank_image_asset_id,
                           BOOL               tentative,
                           BOOL               allow_no_texture,
                           const std::string& label,
                           PermissionMask     immediate_filter_perm_mask,
                           PermissionMask     dnd_filter_perm_mask,
                           PermissionMask     non_immediate_filter_perm_mask,
                           BOOL               can_apply_immediately,
                           LLUIImagePtr       fallback_image_name);

    virtual ~LLFloaterTexturePicker();

    // LLView overrides
    /*virtual*/ BOOL handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType cargo_type, void* cargo_data,
                                       EAcceptance* accept, std::string& tooltip_msg);
    /*virtual*/ void draw();
    /*virtual*/ BOOL handleKeyHere(KEY key, MASK mask);

    // LLFloater overrides
    /*virtual*/ BOOL postBuild();
    /*virtual*/ void onClose(bool app_settings);

    // New functions
    void          setImageID(const LLUUID& image_asset_id, bool set_selection = true);
    void          updateImageStats();
    const LLUUID& getAssetID() { return mImageAssetID; }
    const LLUUID& findItemID(const LLUUID& asset_id, BOOL copyable_only, BOOL ignore_library = FALSE);
    void          setCanApplyImmediately(BOOL b);

    void setActive(BOOL active);

    LLView*        getOwner() const { return mOwner; }
    void           setOwner(LLView* owner) { mOwner = owner; }
    void           stopUsingPipette();
    PermissionMask getFilterPermMask();

    void updateFilterPermMask();
    void commitIfImmediateSet();
    void commitCancel();

    void onFilterEdit(const std::string& search_string);

    void          setCanApply(bool can_preview, bool can_apply);
    void          setTextureSelectedCallback(const texture_selected_callback& cb) { mTextureSelectedCallback = cb; }
    void          setOnFloaterCloseCallback(const floater_close_callback& cb) { mOnFloaterCloseCallback = cb; }
    void          setOnFloaterCommitCallback(const floater_commit_callback& cb) { mOnFloaterCommitCallback = cb; }
    void          setSetImageAssetIDCallback(const set_image_asset_id_callback& cb) { mSetImageAssetIDCallback = cb; }
    void          setOnUpdateImageStatsCallback(const set_on_update_image_stats_callback& cb) { mOnUpdateImageStatsCallback = cb; }
    const LLUUID& getDefaultImageAssetID() { return mDefaultImageAssetID; }
    const LLUUID& getTransparentImageAssetID() { return mTransparentImageAssetID; }
    const LLUUID& getBlankImageAssetID() { return mBlankImageAssetID; }

    static void onBtnSetToDefault(void* userdata);
    static void onBtnSelect(void* userdata);
    static void onBtnCancel(void* userdata);
    void        onBtnPipette();
    // static void		onBtnRevert( void* userdata );
    static void onBtnTransparent(void* userdata);
    static void onBtnBlank(void* userdata);
    static void onBtnNone(void* userdata);
    static void onBtnClear(void* userdata);
    static void onApplyUUID(void* userdata);
    void        onSelectionChange(const std::deque<LLFolderViewItem*>& items, BOOL user_action);
    static void onShowFolders(LLUICtrl* ctrl, void* userdata);
    static void onApplyImmediateCheck(LLUICtrl* ctrl, void* userdata);
    void        onTextureSelect(const LLTextureEntry& te);

    static void onModeSelect(LLUICtrl* ctrl, void* userdata);
    static void onBtnAdd(void* userdata);
    static void onBtnRemove(void* userdata);
    static void onBtnUpload(void* userdata);
    static void onLocalScrollCommit(LLUICtrl* ctrl, void* userdata);

    static void onBakeTextureSelect(LLUICtrl* ctrl, void* userdata);
    static void onHideBaseMeshRegionCheck(LLUICtrl* ctrl, void* userdata);

    void setLocalTextureEnabled(BOOL enabled);
    void setBakeTextureEnabled(BOOL enabled);

  protected:
    LLPointer<LLViewerTexture> mTexturep;
    LLView*                    mOwner;

    LLUUID       mImageAssetID;   // Currently selected texture
    LLUIImagePtr mFallbackImage;  // What to show if currently selected texture is null.
    LLUUID       mDefaultImageAssetID;
    LLUUID       mTransparentImageAssetID;
    LLUUID       mBlankImageAssetID;
    BOOL         mTentative;
    BOOL         mAllowNoTexture;
    LLUUID       mSpecialCurrentImageAssetID;  // Used when the asset id has no corresponding texture in the user's inventory.
    LLUUID       mOriginalImageAssetID;

    std::string mLabel;

    LLTextBox* mTentativeLabel;
    LLTextBox* mResolutionLabel;

    std::string mPendingName;
    BOOL        mActive;

    LLFilterEditor*   mFilterEdit;
    LLInventoryPanel* mInventoryPanel;
    PermissionMask    mImmediateFilterPermMask;
    PermissionMask    mDnDFilterPermMask;
    PermissionMask    mNonImmediateFilterPermMask;
    BOOL              mCanApplyImmediately;
    BOOL              mNoCopyTextureSelected;
    F32               mContextConeOpacity;
    LLSaveFolderState mSavedFolderState;
    BOOL              mSelectedItemPinned;

    LLComboBox*       mModeSelector;
    LLScrollListCtrl* mLocalScrollCtrl;

  private:
    bool mCanApply;
    bool mCanPreview;
    bool mPreviewSettingChanged;

    texture_selected_callback          mTextureSelectedCallback;
    floater_close_callback             mOnFloaterCloseCallback;
    floater_commit_callback            mOnFloaterCommitCallback;
    set_image_asset_id_callback        mSetImageAssetIDCallback;
    set_on_update_image_stats_callback mOnUpdateImageStatsCallback;

    BOOL mBakeTextureEnabled;
};

#endif // LL_FLOATERTEXTUREPICKER_H
