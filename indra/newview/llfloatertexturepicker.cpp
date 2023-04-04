/**
* @file llfloatertexturepicker.cpp
* @author Richard Nelson, James Cook
* @brief LLTextureCtrl class implementation including related functions
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

#include "llagent.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "lldraghandle.h"
#include "llfiltereditor.h"
#include "llfloaterreg.h"
#include "llfloatertexturepicker.h"
#include "llinventoryfunctions.h"
#include "llinventorypanel.h"
#include "llselectmgr.h"
#include "llscrolllistctrl.h"
#include "lltabcontainer.h"
#include "lltoolmgr.h"
#include "lltoolpipette.h"
#include "lltrans.h"
#include "llviewercontrol.h"

#include "llavatarappearancedefines.h"

static const S32 LOCAL_TRACKING_ID_COLUMN = 1;

LLFloaterTexturePicker::LLFloaterTexturePicker(LLView*            owner,
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
                                               LLUIImagePtr       fallback_image) :
    LLFloater(LLSD()),
    mOwner(owner),
    mImageAssetID(image_asset_id),
    mOriginalImageAssetID(image_asset_id),
    mFallbackImage(fallback_image),
    mDefaultImageAssetID(default_image_asset_id),
    mTransparentImageAssetID(transparent_image_asset_id),
    mBlankImageAssetID(blank_image_asset_id),
    mTentative(tentative),
    mAllowNoTexture(allow_no_texture),
    mLabel(label),
    mTentativeLabel(NULL),
    mResolutionLabel(NULL),
    mActive(TRUE),
    mFilterEdit(NULL),
    mImmediateFilterPermMask(immediate_filter_perm_mask),
    mDnDFilterPermMask(dnd_filter_perm_mask),
    mNonImmediateFilterPermMask(non_immediate_filter_perm_mask),
    mContextConeOpacity(0.f),
    mSelectedItemPinned(FALSE),
    mCanApply(true),
    mCanPreview(true),
    mPreviewSettingChanged(false),
    mOnFloaterCommitCallback(NULL),
    mOnFloaterCloseCallback(NULL),
    mSetImageAssetIDCallback(NULL),
    mOnUpdateImageStatsCallback(NULL),
    mBakeTextureEnabled(FALSE)
{
    buildFromFile("floater_texture_ctrl.xml");
    mCanApplyImmediately = can_apply_immediately;
    setCanMinimize(FALSE);
}

LLFloaterTexturePicker::~LLFloaterTexturePicker() {}

void LLFloaterTexturePicker::setImageID(const LLUUID& image_id, bool set_selection /*=true*/)
{
    if (((mImageAssetID != image_id) || mTentative) && mActive)
    {
        mNoCopyTextureSelected = FALSE;
        mViewModel->setDirty();  // *TODO: shouldn't we be using setValue() here?
        mImageAssetID = image_id;

        if (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(mImageAssetID))
        {
            if (mBakeTextureEnabled && mModeSelector->getValue().asInteger() != 2)
            {
                mModeSelector->selectByValue(2);
                onModeSelect(0, this);
            }
        }
        else
        {
            if (mModeSelector->getValue().asInteger() == 2)
            {
                mModeSelector->selectByValue(0);
                onModeSelect(0, this);
            }

            LLUUID item_id = findItemID(mImageAssetID, FALSE);
            if (item_id.isNull())
            {
                mInventoryPanel->getRootFolder()->clearSelection();
            }
            else
            {
                LLInventoryItem* itemp = gInventory.getItem(image_id);
                if (itemp && !itemp->getPermissions().allowCopyBy(gAgent.getID()))
                {
                    // no copy texture
                    getChild<LLUICtrl>("apply_immediate_check")->setValue(FALSE);
                    mNoCopyTextureSelected = TRUE;
                }
            }

            if (set_selection)
            {
                mInventoryPanel->setSelection(item_id, TAKE_FOCUS_NO);
            }
        }
    }
}

void LLFloaterTexturePicker::setActive(BOOL active)
{
    if (!active && getChild<LLUICtrl>("Pipette")->getValue().asBoolean())
    {
        stopUsingPipette();
    }
    mActive = active;
}

void LLFloaterTexturePicker::setCanApplyImmediately(BOOL b)
{
    mCanApplyImmediately = b;
    if (!mCanApplyImmediately)
    {
        getChild<LLUICtrl>("apply_immediate_check")->setValue(FALSE);
    }
    updateFilterPermMask();
}

void LLFloaterTexturePicker::stopUsingPipette()
{
    if (LLToolMgr::getInstance()->getCurrentTool() == LLToolPipette::getInstance())
    {
        LLToolMgr::getInstance()->clearTransientTool();
    }
}

void LLFloaterTexturePicker::updateImageStats()
{
    if (mTexturep.notNull())
    {
        // RN: have we received header data for this image?
        if (mTexturep->getFullWidth() > 0 && mTexturep->getFullHeight() > 0)
        {
            std::string formatted_dims = llformat("%d x %d", mTexturep->getFullWidth(), mTexturep->getFullHeight());
            mResolutionLabel->setTextArg("[DIMENSIONS]", formatted_dims);
            if (mOnUpdateImageStatsCallback)
            {
                mOnUpdateImageStatsCallback(mTexturep);
            }
        }
        else
        {
            mResolutionLabel->setTextArg("[DIMENSIONS]", std::string("[? x ?]"));
        }
    }
    else
    {
        mResolutionLabel->setTextArg("[DIMENSIONS]", std::string(""));
    }
}

// virtual
BOOL LLFloaterTexturePicker::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType cargo_type, void* cargo_data,
                                               EAcceptance* accept, std::string& tooltip_msg)
{
    BOOL handled = FALSE;

    bool is_mesh = cargo_type == DAD_MESH;

    if ((cargo_type == DAD_TEXTURE) || is_mesh)
    {
        LLInventoryItem* item = (LLInventoryItem*) cargo_data;

        BOOL copy = item->getPermissions().allowCopyBy(gAgent.getID());
        BOOL mod  = item->getPermissions().allowModifyBy(gAgent.getID());
        BOOL xfer = item->getPermissions().allowOperationBy(PERM_TRANSFER, gAgent.getID());

        PermissionMask item_perm_mask = 0;
        if (copy)
            item_perm_mask |= PERM_COPY;
        if (mod)
            item_perm_mask |= PERM_MODIFY;
        if (xfer)
            item_perm_mask |= PERM_TRANSFER;

        // PermissionMask filter_perm_mask = getFilterPermMask();  Commented out due to no-copy texture loss.
        PermissionMask filter_perm_mask = mDnDFilterPermMask;
        if ((item_perm_mask & filter_perm_mask) == filter_perm_mask)
        {
            if (drop)
            {
                setImageID(item->getAssetUUID());
                commitIfImmediateSet();
            }

            *accept = ACCEPT_YES_SINGLE;
        }
        else
        {
            *accept = ACCEPT_NO;
        }
    }
    else
    {
        *accept = ACCEPT_NO;
    }

    handled = TRUE;
    LL_DEBUGS("UserInput") << "dragAndDrop handled by LLFloaterTexturePicker " << getName() << LL_ENDL;

    return handled;
}

BOOL LLFloaterTexturePicker::handleKeyHere(KEY key, MASK mask)
{
    LLFolderView* root_folder = mInventoryPanel->getRootFolder();

    if (root_folder && mFilterEdit)
    {
        if (mFilterEdit->hasFocus() && (key == KEY_RETURN || key == KEY_DOWN) && mask == MASK_NONE)
        {
            if (!root_folder->getCurSelectedItem())
            {
                LLFolderViewItem* itemp = mInventoryPanel->getItemByID(gInventory.getRootFolderID());
                if (itemp)
                {
                    root_folder->setSelection(itemp, FALSE, FALSE);
                }
            }
            root_folder->scrollToShowSelection();

            // move focus to inventory proper
            mInventoryPanel->setFocus(TRUE);

            // treat this as a user selection of the first filtered result
            commitIfImmediateSet();

            return TRUE;
        }

        if (mInventoryPanel->hasFocus() && key == KEY_UP)
        {
            mFilterEdit->focusFirstItem(TRUE);
        }
    }

    return LLFloater::handleKeyHere(key, mask);
}

void LLFloaterTexturePicker::onClose(bool app_quitting)
{
    if (mOwner && mOnFloaterCloseCallback)
    {
        mOnFloaterCloseCallback();
    }
    stopUsingPipette();
}

// virtual
BOOL LLFloaterTexturePicker::postBuild()
{
    LLFloater::postBuild();

    if (!mLabel.empty())
    {
        std::string pick = getString("pick title");

        setTitle(pick + mLabel);
    }
    mTentativeLabel = getChild<LLTextBox>("Multiple");

    mResolutionLabel = getChild<LLTextBox>("size_lbl");

    childSetAction("Default", LLFloaterTexturePicker::onBtnSetToDefault, this);
    childSetAction("None", LLFloaterTexturePicker::onBtnNone, this);
    childSetAction("Blank", LLFloaterTexturePicker::onBtnBlank, this);
    childSetAction("Transparent", LLFloaterTexturePicker::onBtnTransparent, this);

    childSetCommitCallback("show_folders_check", onShowFolders, this);
    getChildView("show_folders_check")->setVisible(FALSE);

    mFilterEdit = getChild<LLFilterEditor>("inventory search editor");
    mFilterEdit->setCommitCallback(boost::bind(&LLFloaterTexturePicker::onFilterEdit, this, _2));

    mInventoryPanel = getChild<LLInventoryPanel>("inventory panel");

    mModeSelector = getChild<LLComboBox>("mode_selection");
    mModeSelector->setCommitCallback(onModeSelect, this);
    mModeSelector->selectByValue(0);

    if (mInventoryPanel)
    {
        U32 filter_types = 0x0;
        filter_types |= 0x1 << LLInventoryType::IT_TEXTURE;
        filter_types |= 0x1 << LLInventoryType::IT_SNAPSHOT;

        mInventoryPanel->setFilterTypes(filter_types);
        // mInventoryPanel->setFilterPermMask(getFilterPermMask());  //Commented out due to no-copy texture loss.
        mInventoryPanel->setFilterPermMask(mImmediateFilterPermMask);
        mInventoryPanel->setSelectCallback(boost::bind(&LLFloaterTexturePicker::onSelectionChange, this, _1, _2));
        mInventoryPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);

        // Disable auto selecting first filtered item because it takes away
        // selection from the item set by LLTextureCtrl owning this floater.
        mInventoryPanel->getRootFolder()->setAutoSelectOverride(TRUE);

        // Commented out to scroll to currently selected texture. See EXT-5403.
        // // store this filter as the default one
        // mInventoryPanel->getRootFolder()->getFilter().markDefault();

        // Commented out to stop opening all folders with textures
        // mInventoryPanel->openDefaultFolderForType(LLFolderType::FT_TEXTURE);

        // don't put keyboard focus on selected item, because the selection callback
        // will assume that this was user input

        if (!mImageAssetID.isNull())
        {
            mInventoryPanel->setSelection(findItemID(mImageAssetID, FALSE), TAKE_FOCUS_NO);
        }
    }

    childSetAction("l_add_btn", LLFloaterTexturePicker::onBtnAdd, this);
    childSetAction("l_rem_btn", LLFloaterTexturePicker::onBtnRemove, this);
    childSetAction("l_upl_btn", LLFloaterTexturePicker::onBtnUpload, this);

    mLocalScrollCtrl = getChild<LLScrollListCtrl>("l_name_list");
    mLocalScrollCtrl->setCommitCallback(onLocalScrollCommit, this);
    LLLocalBitmapMgr::getInstance()->feedScrollList(mLocalScrollCtrl);

    getChild<LLLineEditor>("uuid_editor")->setCommitCallback(boost::bind(&onApplyUUID, this));
    getChild<LLButton>("apply_uuid_btn")->setClickedCallback(boost::bind(&onApplyUUID, this));

    mNoCopyTextureSelected = FALSE;

    getChild<LLUICtrl>("apply_immediate_check")->setValue(gSavedSettings.getBOOL("TextureLivePreview"));
    childSetCommitCallback("apply_immediate_check", onApplyImmediateCheck, this);

    if (!mCanApplyImmediately)
    {
        getChildView("show_folders_check")->setEnabled(FALSE);
    }

    getChild<LLUICtrl>("Pipette")->setCommitCallback(boost::bind(&LLFloaterTexturePicker::onBtnPipette, this));
    childSetAction("Cancel", LLFloaterTexturePicker::onBtnCancel, this);
    childSetAction("Select", LLFloaterTexturePicker::onBtnSelect, this);

    // update permission filter once UI is fully initialized
    updateFilterPermMask();
    mSavedFolderState.setApply(FALSE);

    LLToolPipette::getInstance()->setToolSelectCallback(boost::bind(&LLFloaterTexturePicker::onTextureSelect, this, _1));

    getChild<LLComboBox>("l_bake_use_texture_combo_box")->setCommitCallback(onBakeTextureSelect, this);
    getChild<LLCheckBoxCtrl>("hide_base_mesh_region")->setCommitCallback(onHideBaseMeshRegionCheck, this);

    setBakeTextureEnabled(TRUE);
    return TRUE;
}

// virtual
void LLFloaterTexturePicker::draw()
{
    static LLCachedControl<F32> max_opacity(gSavedSettings, "PickerContextOpacity", 0.4f);
    drawConeToOwner(mContextConeOpacity, max_opacity, mOwner);

    updateImageStats();

    // if we're inactive, gray out "apply immediate" checkbox
    getChildView("show_folders_check")->setEnabled(mActive && mCanApplyImmediately && !mNoCopyTextureSelected);
    getChildView("Select")->setEnabled(mActive && mCanApply);
    getChildView("Pipette")->setEnabled(mActive);
    getChild<LLUICtrl>("Pipette")->setValue(LLToolMgr::getInstance()->getCurrentTool() == LLToolPipette::getInstance());

    // BOOL allow_copy = FALSE;
    if (mOwner)
    {
        mTexturep = NULL;
        if (mImageAssetID.notNull())
        {
            LLPointer<LLViewerFetchedTexture> texture = NULL;

            if (LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(mImageAssetID))
            {
                LLViewerObject* obj = LLSelectMgr::getInstance()->getSelection()->getFirstObject();
                if (obj)
                {
                    LLViewerTexture* viewerTexture = obj->getBakedTextureForMagicId(mImageAssetID);
                    texture                        = viewerTexture ? dynamic_cast<LLViewerFetchedTexture*>(viewerTexture) : NULL;
                }
            }

            if (texture.isNull())
            {
                texture = LLViewerTextureManager::getFetchedTexture(mImageAssetID);
            }

            mTexturep = texture;
            mTexturep->setBoostLevel(LLGLTexture::BOOST_PREVIEW);
        }

        if (mTentativeLabel)
        {
            mTentativeLabel->setVisible(FALSE);
        }

        getChildView("Default")->setEnabled(mImageAssetID != mDefaultImageAssetID || mTentative);
        getChildView("Transparent")->setEnabled(mImageAssetID != mTransparentImageAssetID || mTentative);
        getChildView("Blank")->setEnabled(mImageAssetID != mBlankImageAssetID || mTentative);
        getChildView("None")->setEnabled(mAllowNoTexture && (!mImageAssetID.isNull() || mTentative));

        LLFloater::draw();

        if (isMinimized())
        {
            return;
        }

        // Border
        LLRect border = getChildView("preview_widget")->getRect();
        gl_rect_2d(border, LLColor4::black, FALSE);

        // Interior
        LLRect interior = border;
        interior.stretch(-1);

        // If the floater is focused, don't apply its alpha to the texture (STORM-677).
        const F32 alpha = getTransparencyType() == TT_ACTIVE ? 1.0f : getCurrentTransparency();
        if (mTexturep)
        {
            if (mTexturep->getComponents() == 4)
            {
                gl_rect_2d_checkerboard(interior, alpha);
            }

            gl_draw_scaled_image(interior.mLeft, interior.mBottom, interior.getWidth(), interior.getHeight(), mTexturep,
                                 UI_VERTEX_COLOR % alpha);

            // Pump the priority
            mTexturep->addTextureStats((F32) (interior.getWidth() * interior.getHeight()));
        }
        else if (!mFallbackImage.isNull())
        {
            mFallbackImage->draw(interior, UI_VERTEX_COLOR % alpha);
        }
        else
        {
            gl_rect_2d(interior, LLColor4::grey % alpha, TRUE);

            // Draw X
            gl_draw_x(interior, LLColor4::black);
        }

        // Draw Tentative Label over the image
        if (mTentative && !mViewModel->isDirty())
        {
            mTentativeLabel->setVisible(TRUE);
            drawChild(mTentativeLabel);
        }

        if (mSelectedItemPinned)
            return;

        LLFolderView* folder_view = mInventoryPanel->getRootFolder();
        if (!folder_view)
            return;

        LLFolderViewFilter& filter = static_cast<LLFolderViewModelInventory*>(folder_view->getFolderViewModel())->getFilter();

        bool is_filter_active =
            folder_view->getViewModelItem()->getLastFilterGeneration() < filter.getCurrentGeneration() && filter.isNotDefault();

        // After inventory panel filter is applied we have to update
        // constraint rect for the selected item because of folder view
        // AutoSelectOverride set to TRUE. We force PinningSelectedItem
        // flag to FALSE state and setting filter "dirty" to update
        // scroll container to show selected item (see LLFolderView::doIdle()).
        if (!is_filter_active && !mSelectedItemPinned)
        {
            folder_view->setPinningSelectedItem(mSelectedItemPinned);
            folder_view->getViewModelItem()->dirtyFilter();
            mSelectedItemPinned = TRUE;
        }
    }
}

const LLUUID& LLFloaterTexturePicker::findItemID(const LLUUID& asset_id, BOOL copyable_only, BOOL ignore_library)
{
    LLViewerInventoryCategory::cat_array_t cats;
    LLViewerInventoryItem::item_array_t    items;
    LLAssetIDMatches                       asset_id_matches(asset_id);
    gInventory.collectDescendentsIf(LLUUID::null, cats, items, LLInventoryModel::INCLUDE_TRASH, asset_id_matches);

    if (items.size())
    {
        // search for copyable version first
        for (S32 i = 0; i < items.size(); i++)
        {
            LLInventoryItem* itemp            = items[i];
            LLPermissions    item_permissions = itemp->getPermissions();
            if (item_permissions.allowCopyBy(gAgent.getID(), gAgent.getGroupID()))
            {
                if (!ignore_library || !gInventory.isObjectDescendentOf(itemp->getUUID(), gInventory.getLibraryRootFolderID()))
                {
                    return itemp->getUUID();
                }
            }
        }
        // otherwise just return first instance, unless copyable requested
        if (copyable_only)
        {
            return LLUUID::null;
        }
        else
        {
            if (!ignore_library || !gInventory.isObjectDescendentOf(items[0]->getUUID(), gInventory.getLibraryRootFolderID()))
            {
                return items[0]->getUUID();
            }
        }
    }

    return LLUUID::null;
}

PermissionMask LLFloaterTexturePicker::getFilterPermMask()
{
    bool apply_immediate = getChild<LLUICtrl>("apply_immediate_check")->getValue().asBoolean();
    return apply_immediate ? mImmediateFilterPermMask : mNonImmediateFilterPermMask;
}

void LLFloaterTexturePicker::commitIfImmediateSet()
{
    if (!mNoCopyTextureSelected && mOnFloaterCommitCallback && mCanApply)
    {
        mOnFloaterCommitCallback(LLTextureCtrl::TEXTURE_CHANGE, LLUUID::null);
    }
}

void LLFloaterTexturePicker::commitCancel()
{
    if (!mNoCopyTextureSelected && mOnFloaterCommitCallback && mCanApply)
    {
        mOnFloaterCommitCallback(LLTextureCtrl::TEXTURE_CANCEL, LLUUID::null);
    }
}

// static
void LLFloaterTexturePicker::onBtnSetToDefault(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    self->setCanApply(true, true);
    if (self->mOwner)
    {
        self->setImageID(self->getDefaultImageAssetID());
    }
    self->commitIfImmediateSet();
}

// static
void LLFloaterTexturePicker::onBtnTransparent(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    self->setCanApply(true, true);
    self->setImageID(self->getTransparentImageAssetID());
    self->commitIfImmediateSet();
}

// static
void LLFloaterTexturePicker::onBtnBlank(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    self->setCanApply(true, true);
    self->setImageID(self->getBlankImageAssetID());
    self->commitIfImmediateSet();
}

// static
void LLFloaterTexturePicker::onBtnNone(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    self->setImageID(LLUUID::null);
    self->commitCancel();
}

/*
// static
void LLFloaterTexturePicker::onBtnRevert(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    self->setImageID( self->mOriginalImageAssetID );
    // TODO: Change this to tell the owner to cancel.  It needs to be
    // smart enough to restore multi-texture selections.
    self->mOwner->onFloaterCommit();
    self->mViewModel->resetDirty();
}*/

// static
void LLFloaterTexturePicker::onBtnCancel(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    self->setImageID(self->mOriginalImageAssetID);
    if (self->mOnFloaterCommitCallback)
    {
        self->mOnFloaterCommitCallback(LLTextureCtrl::TEXTURE_CANCEL, LLUUID::null);
    }
    self->mViewModel->resetDirty();
    self->closeFloater();
}

// static
void LLFloaterTexturePicker::onBtnSelect(void* userdata)
{
    LLFloaterTexturePicker* self     = (LLFloaterTexturePicker*) userdata;
    LLUUID                  local_id = LLUUID::null;
    if (self->mOwner)
    {
        if (self->mLocalScrollCtrl->getVisible() && !self->mLocalScrollCtrl->getAllSelected().empty())
        {
            LLUUID temp_id = self->mLocalScrollCtrl->getFirstSelected()->getColumn(LOCAL_TRACKING_ID_COLUMN)->getValue().asUUID();
            local_id       = LLLocalBitmapMgr::getInstance()->getWorldID(temp_id);
        }
    }

    if (self->mOnFloaterCommitCallback)
    {
        self->mOnFloaterCommitCallback(LLTextureCtrl::TEXTURE_SELECT, local_id);
    }
    self->closeFloater();
}

void LLFloaterTexturePicker::onBtnPipette()
{
    BOOL pipette_active = getChild<LLUICtrl>("Pipette")->getValue().asBoolean();
    pipette_active      = !pipette_active;
    if (pipette_active)
    {
        LLToolMgr::getInstance()->setTransientTool(LLToolPipette::getInstance());
    }
    else
    {
        LLToolMgr::getInstance()->clearTransientTool();
    }
}

// static
void LLFloaterTexturePicker::onApplyUUID(void* userdata)
{
    LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
    LLUUID                  id(self->getChild<LLLineEditor>("uuid_editor")->getText());
    if (id.notNull())
    {
        self->setImageID(id);
        self->commitIfImmediateSet();
    }
}

void LLFloaterTexturePicker::onSelectionChange(const std::deque<LLFolderViewItem*>& items, BOOL user_action)
{
    if (items.size())
    {
        LLFolderViewItem* first_item = items.front();
        LLInventoryItem*  itemp =
            gInventory.getItem(static_cast<LLFolderViewModelItemInventory*>(first_item->getViewModelItem())->getUUID());
        mNoCopyTextureSelected = FALSE;
        if (itemp)
        {
            if (!mTextureSelectedCallback.empty())
            {
                mTextureSelectedCallback(itemp);
            }
            if (!itemp->getPermissions().allowCopyBy(gAgent.getID()))
            {
                mNoCopyTextureSelected = TRUE;
            }
            setImageID(itemp->getAssetUUID(), false);
            mViewModel->setDirty();  // *TODO: shouldn't we be using setValue() here?

            if (!mPreviewSettingChanged)
            {
                mCanPreview = gSavedSettings.getBOOL("TextureLivePreview");
            }
            else
            {
                mPreviewSettingChanged = false;
            }

            if (user_action && mCanPreview)
            {
                // only commit intentional selections, not implicit ones
                commitIfImmediateSet();
            }
        }
    }
}

// static
void LLFloaterTexturePicker::onModeSelect(LLUICtrl* ctrl, void* userdata)
{
    LLFloaterTexturePicker* self  = (LLFloaterTexturePicker*) userdata;
    int                     index = self->mModeSelector->getValue().asInteger();

    self->getChild<LLButton>("Default")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLButton>("Transparent")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLButton>("Blank")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLButton>("None")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLButton>("Pipette")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLFilterEditor>("inventory search editor")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLInventoryPanel>("inventory panel")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLLineEditor>("uuid_editor")->setVisible(index == 0 ? TRUE : FALSE);
    self->getChild<LLButton>("apply_uuid_btn")->setVisible(index == 0 ? TRUE : FALSE);

    /*self->getChild<LLCheckBox>("show_folders_check")->setVisible(mode);
      no idea under which conditions the above is even shown, needs testing. */

    self->getChild<LLButton>("l_add_btn")->setVisible(index == 1 ? TRUE : FALSE);
    self->getChild<LLButton>("l_rem_btn")->setVisible(index == 1 ? TRUE : FALSE);
    self->getChild<LLButton>("l_upl_btn")->setVisible(index == 1 ? TRUE : FALSE);
    self->getChild<LLScrollListCtrl>("l_name_list")->setVisible(index == 1 ? TRUE : FALSE);

    self->getChild<LLComboBox>("l_bake_use_texture_combo_box")->setVisible(index == 2 ? TRUE : FALSE);
    self->getChild<LLCheckBoxCtrl>("hide_base_mesh_region")->setVisible(FALSE);  // index == 2 ? TRUE : FALSE);

    if (index == 2)
    {
        self->stopUsingPipette();

        S8 val = -1;

        LLUUID imageID = self->mImageAssetID;
        if (imageID == IMG_USE_BAKED_HEAD)
        {
            val = 0;
        }
        else if (imageID == IMG_USE_BAKED_UPPER)
        {
            val = 1;
        }
        else if (imageID == IMG_USE_BAKED_LOWER)
        {
            val = 2;
        }
        else if (imageID == IMG_USE_BAKED_EYES)
        {
            val = 3;
        }
        else if (imageID == IMG_USE_BAKED_SKIRT)
        {
            val = 4;
        }
        else if (imageID == IMG_USE_BAKED_HAIR)
        {
            val = 5;
        }
        else if (imageID == IMG_USE_BAKED_LEFTARM)
        {
            val = 6;
        }
        else if (imageID == IMG_USE_BAKED_LEFTLEG)
        {
            val = 7;
        }
        else if (imageID == IMG_USE_BAKED_AUX1)
        {
            val = 8;
        }
        else if (imageID == IMG_USE_BAKED_AUX2)
        {
            val = 9;
        }
        else if (imageID == IMG_USE_BAKED_AUX3)
        {
            val = 10;
        }

        self->getChild<LLComboBox>("l_bake_use_texture_combo_box")->setSelectedByValue(val, TRUE);
    }
}

// static
void LLFloaterTexturePicker::onBtnAdd(void* userdata)
{
    if (LLLocalBitmapMgr::getInstance()->addUnit() == true)
    {
        LLFloaterTexturePicker* self = (LLFloaterTexturePicker*) userdata;
        LLLocalBitmapMgr::getInstance()->feedScrollList(self->mLocalScrollCtrl);
    }
}

// static
void LLFloaterTexturePicker::onBtnRemove(void* userdata)
{
    LLFloaterTexturePicker*        self           = (LLFloaterTexturePicker*) userdata;
    std::vector<LLScrollListItem*> selected_items = self->mLocalScrollCtrl->getAllSelected();

    if (!selected_items.empty())
    {
        for (std::vector<LLScrollListItem*>::iterator iter = selected_items.begin(); iter != selected_items.end(); iter++)
        {
            LLScrollListItem* list_item = *iter;
            if (list_item)
            {
                LLUUID tracking_id = list_item->getColumn(LOCAL_TRACKING_ID_COLUMN)->getValue().asUUID();
                LLLocalBitmapMgr::getInstance()->delUnit(tracking_id);
            }
        }

        self->getChild<LLButton>("l_rem_btn")->setEnabled(false);
        self->getChild<LLButton>("l_upl_btn")->setEnabled(false);
        LLLocalBitmapMgr::getInstance()->feedScrollList(self->mLocalScrollCtrl);
    }
}

// static
void LLFloaterTexturePicker::onBtnUpload(void* userdata)
{
    LLFloaterTexturePicker*        self           = (LLFloaterTexturePicker*) userdata;
    std::vector<LLScrollListItem*> selected_items = self->mLocalScrollCtrl->getAllSelected();

    if (selected_items.empty())
    {
        return;
    }

    /* currently only allows uploading one by one, picks the first item from the selection list.  (not the vector!)
       in the future, it might be a good idea to check the vector size and if more than one units is selected - opt for multi-image upload.
     */

    LLUUID      tracking_id = (LLUUID) self->mLocalScrollCtrl->getSelectedItemLabel(LOCAL_TRACKING_ID_COLUMN);
    std::string filename    = LLLocalBitmapMgr::getInstance()->getFilename(tracking_id);

    if (!filename.empty())
    {
        LLFloaterReg::showInstance("upload_image", LLSD(filename));
    }
}

// static
void LLFloaterTexturePicker::onLocalScrollCommit(LLUICtrl* ctrl, void* userdata)
{
    LLFloaterTexturePicker*        self           = (LLFloaterTexturePicker*) userdata;
    std::vector<LLScrollListItem*> selected_items = self->mLocalScrollCtrl->getAllSelected();
    bool                           has_selection  = !selected_items.empty();

    self->getChild<LLButton>("l_rem_btn")->setEnabled(has_selection);
    self->getChild<LLButton>("l_upl_btn")->setEnabled(has_selection && (selected_items.size() < 2));
    /* since multiple-localbitmap upload is not implemented, upl button gets disabled if more than one is selected. */

    if (has_selection)
    {
        LLUUID tracking_id = (LLUUID) self->mLocalScrollCtrl->getSelectedItemLabel(LOCAL_TRACKING_ID_COLUMN);
        LLUUID inworld_id  = LLLocalBitmapMgr::getInstance()->getWorldID(tracking_id);
        if (self->mSetImageAssetIDCallback)
        {
            self->mSetImageAssetIDCallback(inworld_id);
        }

        if (self->childGetValue("apply_immediate_check").asBoolean())
        {
            if (self->mOnFloaterCommitCallback)
            {
                self->mOnFloaterCommitCallback(LLTextureCtrl::TEXTURE_CHANGE, inworld_id);
            }
        }
    }
}

// static
void LLFloaterTexturePicker::onShowFolders(LLUICtrl* ctrl, void* user_data)
{
    LLCheckBoxCtrl*         check_box = (LLCheckBoxCtrl*) ctrl;
    LLFloaterTexturePicker* picker    = (LLFloaterTexturePicker*) user_data;

    if (check_box->get())
    {
        picker->mInventoryPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
    }
    else
    {
        picker->mInventoryPanel->setShowFolderState(LLInventoryFilter::SHOW_NO_FOLDERS);
    }
}

// static
void LLFloaterTexturePicker::onApplyImmediateCheck(LLUICtrl* ctrl, void* user_data)
{
    LLFloaterTexturePicker* picker = (LLFloaterTexturePicker*) user_data;

    LLCheckBoxCtrl* check_box = (LLCheckBoxCtrl*) ctrl;
    gSavedSettings.setBOOL("TextureLivePreview", check_box->get());

    picker->updateFilterPermMask();
    picker->commitIfImmediateSet();
}

// static
void LLFloaterTexturePicker::onBakeTextureSelect(LLUICtrl* ctrl, void* user_data)
{
    LLFloaterTexturePicker* self      = (LLFloaterTexturePicker*) user_data;
    LLComboBox*             combo_box = (LLComboBox*) ctrl;

    S8 type = combo_box->getValue().asInteger();

    LLUUID imageID = self->mDefaultImageAssetID;
    if (type == 0)
    {
        imageID = IMG_USE_BAKED_HEAD;
    }
    else if (type == 1)
    {
        imageID = IMG_USE_BAKED_UPPER;
    }
    else if (type == 2)
    {
        imageID = IMG_USE_BAKED_LOWER;
    }
    else if (type == 3)
    {
        imageID = IMG_USE_BAKED_EYES;
    }
    else if (type == 4)
    {
        imageID = IMG_USE_BAKED_SKIRT;
    }
    else if (type == 5)
    {
        imageID = IMG_USE_BAKED_HAIR;
    }
    else if (type == 6)
    {
        imageID = IMG_USE_BAKED_LEFTARM;
    }
    else if (type == 7)
    {
        imageID = IMG_USE_BAKED_LEFTLEG;
    }
    else if (type == 8)
    {
        imageID = IMG_USE_BAKED_AUX1;
    }
    else if (type == 9)
    {
        imageID = IMG_USE_BAKED_AUX2;
    }
    else if (type == 10)
    {
        imageID = IMG_USE_BAKED_AUX3;
    }

    self->setImageID(imageID);
    self->mViewModel->setDirty();  // *TODO: shouldn't we be using setValue() here?

    if (!self->mPreviewSettingChanged)
    {
        self->mCanPreview = gSavedSettings.getBOOL("TextureLivePreview");
    }
    else
    {
        self->mPreviewSettingChanged = false;
    }

    if (self->mCanPreview)
    {
        // only commit intentional selections, not implicit ones
        self->commitIfImmediateSet();
    }
}

// static
void LLFloaterTexturePicker::onHideBaseMeshRegionCheck(LLUICtrl* ctrl, void* user_data)
{
    // LLFloaterTexturePicker* picker = (LLFloaterTexturePicker*)user_data;
    // LLCheckBoxCtrl* check_box = (LLCheckBoxCtrl*)ctrl;
}

void LLFloaterTexturePicker::updateFilterPermMask()
{
    // mInventoryPanel->setFilterPermMask( getFilterPermMask() );  Commented out due to no-copy texture loss.
}

void LLFloaterTexturePicker::setCanApply(bool can_preview, bool can_apply)
{
    getChildRef<LLUICtrl>("Select").setEnabled(can_apply);
    getChildRef<LLUICtrl>("preview_disabled").setVisible(!can_preview);
    getChildRef<LLUICtrl>("apply_immediate_check").setVisible(can_preview);

    mCanApply              = can_apply;
    mCanPreview            = can_preview ? gSavedSettings.getBOOL("TextureLivePreview") : false;
    mPreviewSettingChanged = true;
}

void LLFloaterTexturePicker::onFilterEdit(const std::string& search_string)
{
    std::string upper_case_search_string = search_string;
    LLStringUtil::toUpper(upper_case_search_string);

    if (upper_case_search_string.empty())
    {
        if (mInventoryPanel->getFilterSubString().empty())
        {
            // current filter and new filter empty, do nothing
            return;
        }

        mSavedFolderState.setApply(TRUE);
        mInventoryPanel->getRootFolder()->applyFunctorRecursively(mSavedFolderState);
        // add folder with current item to list of previously opened folders
        LLOpenFoldersWithSelection opener;
        mInventoryPanel->getRootFolder()->applyFunctorRecursively(opener);
        mInventoryPanel->getRootFolder()->scrollToShowSelection();
    }
    else if (mInventoryPanel->getFilterSubString().empty())
    {
        // first letter in search term, save existing folder open state
        if (!mInventoryPanel->getFilter().isNotDefault())
        {
            mSavedFolderState.setApply(FALSE);
            mInventoryPanel->getRootFolder()->applyFunctorRecursively(mSavedFolderState);
        }
    }

    mInventoryPanel->setFilterSubString(search_string);
}

void LLFloaterTexturePicker::setLocalTextureEnabled(BOOL enabled) { mModeSelector->setEnabledByValue(1, enabled); }

void LLFloaterTexturePicker::setBakeTextureEnabled(BOOL enabled)
{
    BOOL changed = (enabled != mBakeTextureEnabled);

    mBakeTextureEnabled = enabled;
    mModeSelector->setEnabledByValue(2, enabled);

    if (!mBakeTextureEnabled && (mModeSelector->getValue().asInteger() == 2))
    {
        mModeSelector->selectByValue(0);
    }

    if (changed && mBakeTextureEnabled && LLAvatarAppearanceDefines::LLAvatarAppearanceDictionary::isBakedImageId(mImageAssetID))
    {
        if (mModeSelector->getValue().asInteger() != 2)
        {
            mModeSelector->selectByValue(2);
        }
    }
    onModeSelect(0, this);
}

void LLFloaterTexturePicker::onTextureSelect(const LLTextureEntry& te)
{
    LLUUID inventory_item_id = findItemID(te.getID(), TRUE);
    if (inventory_item_id.notNull())
    {
        LLToolPipette::getInstance()->setResult(TRUE, "");
        setImageID(te.getID());

        mNoCopyTextureSelected = FALSE;
        LLInventoryItem* itemp = gInventory.getItem(inventory_item_id);

        if (itemp && !itemp->getPermissions().allowCopyBy(gAgent.getID()))
        {
            // no copy texture
            mNoCopyTextureSelected = TRUE;
        }

        commitIfImmediateSet();
    }
    else
    {
        LLToolPipette::getInstance()->setResult(FALSE, LLTrans::getString("InventoryNoTexture"));
    }
}
