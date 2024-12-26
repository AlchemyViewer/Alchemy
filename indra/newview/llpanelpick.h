/**
 * @file llpanelpick.h
 * @brief LLPanelPick class definition
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
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

// Display of a "Top Pick" used both for the global top picks in the
// Find directory, and also for each individual user's picks in their
// profile.

#ifndef LL_LLPANELPICK_H
#define LL_LLPANELPICK_H

#include "llpanel.h"
#include "llremoteparcelrequest.h"
#include "llavatarpropertiesprocessor.h"

class LLIconCtrl;
class LLTextureCtrl;
class LLScrollContainer;
class LLMessageSystem;
class LLAvatarPropertiesObserver;

/**
 * Panel for displaying Pick Information - snapshot, name, description, etc.
 */
class LLPanelPickInfo : public LLPanel, public LLAvatarPropertiesObserver, LLRemoteParcelInfoObserver
{
    LOG_CLASS(LLPanelPickInfo);
public:

    // Creates new panel
    static LLPanelPickInfo* create();

    virtual ~LLPanelPickInfo();

    /**
     * Initializes panel properties
     *
     * By default Pick will be created for current Agent location.
     * Use setPickData to change Pick properties.
     */
    /*virtual*/ void onOpen(const LLSD& key) override;

    /*virtual*/ BOOL postBuild() override;

    /*virtual*/ void reshape(S32 width, S32 height, BOOL called_from_parent = TRUE) override;

    /*virtual*/ void processProperties(void* data, EAvatarProcessorType type) override;

    /**
     * Sends remote parcel info request to resolve parcel name from its ID.
     */
    void sendParcelInfoRequest();

    /**
     * Sets "Back" button click callback
     */
    virtual void setExitCallback(const commit_callback_t& cb);

    /**
     * Sets "Edit" button click callback
     */
    virtual void setEditPickCallback(const commit_callback_t& cb);

    //This stuff we got from LLRemoteParcelObserver, in the last one we intentionally do nothing
    /*virtual*/ void processParcelInfo(const LLParcelData& parcel_data) override;
    /*virtual*/ void setParcelID(const LLUUID& parcel_id) override { mParcelId = parcel_id; }
    /*virtual*/ void setErrorStatus(S32 status, const std::string& reason) override {};

protected:

    LLPanelPickInfo();

    /**
     * Resets Pick information
     */
    virtual void resetData();

    /**
     * Resets UI controls (visibility, values)
     */
    virtual void resetControls();

    /**
    * "Location text" is actually the owner name, the original
    * name that owner gave the parcel, and the location.
    */
    static std::string createLocationText(
        const std::string& owner_name,
        const std::string& original_name,
        const std::string& sim_name,
        const LLVector3d& pos_global);

    virtual void setAvatarId(const LLUUID& avatar_id) { mAvatarId = avatar_id; }
    virtual LLUUID& getAvatarId() { return mAvatarId; }

    /**
     * Sets snapshot id.
     *
     * Will mark snapshot control as valid if id is not null.
     * Will mark snapshot control as invalid if id is null. If null id is a valid value,
     * you have to manually mark snapshot is valid.
     */
    virtual void setSnapshotId(const LLUUID& id);

    virtual void setPickId(const LLUUID& id) { mPickId = id; }
    virtual LLUUID& getPickId() { return mPickId; }

    virtual void setPickName(const std::string& name);

    virtual void setPickDesc(const std::string& desc);

    virtual void setPickLocation(const std::string& location);

    virtual void setPosGlobal(const LLVector3d& pos) { mPosGlobal = pos; }
    virtual LLVector3d& getPosGlobal() { return mPosGlobal; }

    /**
     * Callback for "Map" button, opens Map
     */
    void onClickMap();

    /**
     * Callback for "Teleport" button, teleports user to Pick location.
     */
    void onClickTeleport();

    void onClickBack();

    S32                     mScrollingPanelMinHeight;
    S32                     mScrollingPanelWidth;
    LLScrollContainer*      mScrollContainer;
    LLPanel*                mScrollingPanel;
    LLTextureCtrl*          mSnapshotCtrl;

    LLUUID mAvatarId;
    LLVector3d mPosGlobal;
    LLUUID mParcelId;
    LLUUID mPickId;
    LLUUID mRequestedId;
};

/**
 * Panel for creating/editing Pick.
 */
class LLPanelPickEdit final : public LLPanelPickInfo
{
    LOG_CLASS(LLPanelPickEdit);
public:

    /**
     * Creates new panel
     */
    static LLPanelPickEdit* create();

    ~LLPanelPickEdit() = default;
    void onOpen(const LLSD& key) override;
    virtual void setPickData(const LLPickData* pick_data);
    BOOL postBuild() override;

    /**
     * Sets "Save" button click callback
     */
    virtual void setSaveCallback(const commit_callback_t& cb);

    /**
     * Sets "Cancel" button click callback
     */
    virtual void setCancelCallback(const commit_callback_t& cb);

    /**
     * Resets panel and all cantrols to unedited state
     */
    void resetDirty() override;

    /**
     * Returns true if any of Pick properties was changed by user.
     */
    BOOL isDirty() const override;

    void processProperties(void* data, EAvatarProcessorType type) override;

    /**
    * Sends Pick properties to server.
    */
    void sendUpdate();

protected:

    LLPanelPickEdit();

    /**
     * Called when snapshot image changes.
     */
    void onSnapshotChanged();

    /**
     * Callback for Pick snapshot, name and description changed event.
     */
    void onPickChanged(LLUICtrl* ctrl);

    /*virtual*/ void resetData() override;

    /**
     * Enables/disables "Save" button
     */
    void enableSaveButton(bool enable);

    /**
     * Callback for "Set Location" button click
     */
    void onClickSetLocation();

    /**
     * Callback for "Save" button click
     */
    void onClickSave();

    std::string getLocationNotice();

    bool mLocationChanged;
    bool mNeedData;
    bool mNewPick;

private:

    void initTexturePickerMouseEvents();
        void onTexturePickerMouseEnter(LLUICtrl* ctrl);
    void onTexturePickerMouseLeave(LLUICtrl* ctrl);

    LLIconCtrl* text_icon;
};

#endif // LL_LLPANELPICK_H
