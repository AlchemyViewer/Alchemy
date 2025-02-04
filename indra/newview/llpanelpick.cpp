/**
 * @file llpanelpick.cpp
 * @brief LLPanelPick class implementation
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

#include "llviewerprecompiledheaders.h"

#include "llpanelpick.h"

#include "message.h"

#include "llparcel.h"

#include "llbutton.h"
#include "llfloaterreg.h"
#include "lliconctrl.h"
#include "lllineeditor.h"
#include "llpanel.h"
#include "llscrollcontainer.h"
#include "lltexteditor.h"

#include "llagent.h"
#include "llagentpicksinfo.h"
#include "llavatarpropertiesprocessor.h"
#include "llfloaterworldmap.h"
#include "lltexturectrl.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"

static const std::string XML_PANEL_EDIT_PICK("panel_edit_pick.xml");
static const std::string XML_PANEL_PICK_INFO("panel_pick_info.xml");

static const std::string XML_NAME("pick_name");
static const std::string XML_DESC("pick_desc");
static const std::string XML_SNAPSHOT("pick_snapshot");
static const std::string XML_LOCATION("pick_location");

static const std::string XML_BTN_ON_TXTR("edit_icon");
static const std::string XML_BTN_SAVE("save_changes_btn");

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//static
LLPanelPickInfo* LLPanelPickInfo::create()
{
    LLPanelPickInfo* panel = new LLPanelPickInfo();
    panel->buildFromFile(XML_PANEL_PICK_INFO);
    return panel;
}

LLPanelPickInfo::LLPanelPickInfo()
 : LLPanel()
 , LLAvatarPropertiesObserver()
 , LLRemoteParcelInfoObserver()
 , mScrollingPanelMinHeight(0)
 , mScrollingPanelWidth(0)
 , mScrollContainer(nullptr)
 , mScrollingPanel(nullptr)
 , mSnapshotCtrl(nullptr)
 , mAvatarId(LLUUID::null)
 , mParcelId(LLUUID::null)
 , mPickId(LLUUID::null)
 , mRequestedId(LLUUID::null)
{
}

LLPanelPickInfo::~LLPanelPickInfo()
{
    LLAvatarPropertiesProcessor::getInstance()->removeObserver(LLPanelPickInfo::getAvatarId(), this);

    if (mParcelId.notNull())
    {
        LLRemoteParcelInfoProcessor::getInstance()->removeObserver(mParcelId, this);
    }
}

void LLPanelPickInfo::onOpen(const LLSD& key)
{
    LLUUID avatar_id = key["avatar_id"];
    if(avatar_id.isNull())
    {
        return;
    }

    if(getAvatarId().notNull())
    {
        LLAvatarPropertiesProcessor::getInstance()->removeObserver(
            getAvatarId(), this);
    }

    setAvatarId(avatar_id);

    resetData();
    resetControls();

    setPickId(key["pick_id"]);
    setPickName(key["pick_name"]);
    setPickDesc(key["pick_desc"]);
    setSnapshotId(key["snapshot_id"]);

    LLAvatarPropertiesProcessor::getInstance()->addObserver(
        getAvatarId(), this);
    LLAvatarPropertiesProcessor::getInstance()->sendPickInfoRequest(
        getAvatarId(), getPickId());
}

BOOL LLPanelPickInfo::postBuild()
{
    mSnapshotCtrl = getChild<LLTextureCtrl>(XML_SNAPSHOT);

    childSetAction("teleport_btn", boost::bind(&LLPanelPickInfo::onClickTeleport, this));
    childSetAction("show_on_map_btn", boost::bind(&LLPanelPickInfo::onClickMap, this));
    childSetAction("back_btn", boost::bind(&LLPanelPickInfo::onClickBack, this));

    mScrollingPanel = getChild<LLPanel>("scroll_content_panel");
    mScrollContainer = getChild<LLScrollContainer>("profile_scroll");

    mScrollingPanelMinHeight = mScrollContainer->getScrolledViewRect().getHeight();
    mScrollingPanelWidth = mScrollingPanel->getRect().getWidth();

    LLTextEditor* text_desc = getChild<LLTextEditor>(XML_DESC);
    text_desc->setContentTrusted(false);

    return TRUE;
}

void LLPanelPickInfo::reshape(S32 width, S32 height, BOOL called_from_parent)
{
    LLPanel::reshape(width, height, called_from_parent);

    if (!mScrollContainer || !mScrollingPanel)
        return;

    static LLUICachedControl<S32> scrollbar_size ("UIScrollbarSize", 0);

    S32 scroll_height = mScrollContainer->getRect().getHeight();
    if (mScrollingPanelMinHeight >= scroll_height)
    {
        mScrollingPanel->reshape(mScrollingPanelWidth, mScrollingPanelMinHeight);
    }
    else
    {
        mScrollingPanel->reshape(mScrollingPanelWidth + scrollbar_size, scroll_height);
    }
}

void LLPanelPickInfo::processProperties(void* data, EAvatarProcessorType type)
{
    if(APT_PICK_INFO != type)
    {
        return;
    }
    LLPickData* pick_info = static_cast<LLPickData*>(data);
    if(!pick_info
        || pick_info->creator_id != getAvatarId()
        || pick_info->pick_id != getPickId())
    {
        return;
    }

    mParcelId = pick_info->parcel_id;
    setSnapshotId(pick_info->snapshot_id);
    setPickName(pick_info->name);
    setPickDesc(pick_info->desc);
    setPosGlobal(pick_info->pos_global);

    // Send remote parcel info request to get parcel name and sim (region) name.
    sendParcelInfoRequest();

    // *NOTE dzaporozhan
    // We want to keep listening to APT_PICK_INFO because user may
    // edit the Pick and we have to update Pick info panel.
    // revomeObserver is called from onClickBack
}

void LLPanelPickInfo::sendParcelInfoRequest()
{
    if (mParcelId != mRequestedId)
    {
        LLRemoteParcelInfoProcessor::getInstance()->addObserver(mParcelId, this);
        LLRemoteParcelInfoProcessor::getInstance()->sendParcelInfoRequest(mParcelId);

        mRequestedId = mParcelId;
    }
}

void LLPanelPickInfo::setExitCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("back_btn")->setClickedCallback(cb);
}

void LLPanelPickInfo::processParcelInfo(const LLParcelData& parcel_data)
{
    setPickLocation(createLocationText(LLStringUtil::null, parcel_data.name,
        parcel_data.sim_name, getPosGlobal()));

    // We have received parcel info for the requested ID so clear it now.
    mRequestedId.setNull();

    if (mParcelId.notNull())
    {
        LLRemoteParcelInfoProcessor::getInstance()->removeObserver(mParcelId, this);
    }
}

void LLPanelPickInfo::setEditPickCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("edit_btn")->setClickedCallback(cb);
}

// PROTECTED AREA

void LLPanelPickInfo::resetControls()
{
    if(getAvatarId() == gAgent.getID())
    {
        getChildView("edit_btn")->setEnabled(TRUE);
        getChildView("edit_btn")->setVisible( TRUE);
    }
    else
    {
        getChildView("edit_btn")->setEnabled(FALSE);
        getChildView("edit_btn")->setVisible( FALSE);
    }
}

void LLPanelPickInfo::resetData()
{
    setPickName(LLStringUtil::null);
    setPickDesc(LLStringUtil::null);
    setPickLocation(LLStringUtil::null);
    setPickId(LLUUID::null);
    setSnapshotId(LLUUID::null);
    mPosGlobal.clearVec();
    mParcelId.setNull();
    mRequestedId.setNull();
}

// static
std::string LLPanelPickInfo::createLocationText(const std::string& owner_name, const std::string& original_name, const std::string& sim_name, const LLVector3d& pos_global)
{
    std::string location_text;
    location_text.append(owner_name);
    if (!original_name.empty())
    {
        if (!location_text.empty()) location_text.append(", ");
        location_text.append(original_name);

    }
    if (!sim_name.empty())
    {
        if (!location_text.empty()) location_text.append(", ");
        location_text.append(sim_name);
    }

    if (!location_text.empty()) location_text.append(" ");

    if (!pos_global.isNull())
    {
        S32 region_x = ll_round((F32)pos_global.mdV[VX]) % REGION_WIDTH_UNITS;
        S32 region_y = ll_round((F32)pos_global.mdV[VY]) % REGION_WIDTH_UNITS;
        S32 region_z = ll_round((F32)pos_global.mdV[VZ]);
        location_text.append(llformat(" (%d, %d, %d)", region_x, region_y, region_z));
    }
    return location_text;
}

void LLPanelPickInfo::setSnapshotId(const LLUUID& id)
{
    mSnapshotCtrl->setImageAssetID(id);
    mSnapshotCtrl->setValid(TRUE);
}

void LLPanelPickInfo::setPickName(const std::string& name)
{
    getChild<LLUICtrl>(XML_NAME)->setValue(name);
}

void LLPanelPickInfo::setPickDesc(const std::string& desc)
{
    getChild<LLUICtrl>(XML_DESC)->setValue(desc);
}

void LLPanelPickInfo::setPickLocation(const std::string& location)
{
    getChild<LLUICtrl>(XML_LOCATION)->setValue(location);
}

void LLPanelPickInfo::onClickMap()
{
    LLFloaterWorldMap::getInstance()->trackLocation(getPosGlobal());
    LLFloaterReg::showInstance("world_map", "center");
}

void LLPanelPickInfo::onClickTeleport()
{
    if (!getPosGlobal().isExactlyZero())
    {
        gAgent.teleportViaLocation(getPosGlobal());
        LLFloaterWorldMap::getInstance()->trackLocation(getPosGlobal());
    }
}

void LLPanelPickInfo::onClickBack()
{
    LLAvatarPropertiesProcessor::getInstance()->removeObserver(getAvatarId(), this);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//static
LLPanelPickEdit* LLPanelPickEdit::create()
{
    LLPanelPickEdit* panel = new LLPanelPickEdit();
    panel->buildFromFile(XML_PANEL_EDIT_PICK);
    return panel;
}

LLPanelPickEdit::LLPanelPickEdit()
 : LLPanelPickInfo()
 , mLocationChanged(false)
 , mNeedData(true)
 , mNewPick(false)
 , text_icon(nullptr)
{
}

void LLPanelPickEdit::onOpen(const LLSD& key)
{
    LLUUID pick_id = key["pick_id"];
    mNeedData = true;

    // creating new Pick
    if(pick_id.isNull())
    {
        mNewPick = true;

        setAvatarId(gAgent.getID());

        resetData();
        resetControls();

        setPosGlobal(gAgent.getPositionGlobal());

        LLUUID parcel_id = LLUUID::null, snapshot_id = LLUUID::null;
        std::string pick_name, pick_desc, region_name;

        LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
        if(parcel)
        {
            parcel_id = parcel->getID();
            pick_name = parcel->getName();
            pick_desc = parcel->getDesc();
            snapshot_id = parcel->getSnapshotID();
        }

        LLViewerRegion* region = gAgent.getRegion();
        if(region)
        {
            region_name = region->getName();
        }

        setParcelID(parcel_id);
        getChild<LLUICtrl>("pick_name")->setValue(pick_name.empty() ? region_name : pick_name);
        getChild<LLUICtrl>("pick_desc")->setValue(pick_desc);
        setSnapshotId(snapshot_id);
        setPickLocation(createLocationText(getLocationNotice(), pick_name, region_name, getPosGlobal()));

        enableSaveButton(true);
    }
    // editing existing pick
    else
    {
        mNewPick = false;
        LLPanelPickInfo::onOpen(key);

        enableSaveButton(false);
    }

    resetDirty();
}

void LLPanelPickEdit::setPickData(const LLPickData* pick_data)
{
    if(!pick_data)
    {
        return;
    }

    mNeedData = false;

    setParcelID(pick_data->parcel_id);
    getChild<LLUICtrl>("pick_name")->setValue(pick_data->name);
    getChild<LLUICtrl>("pick_desc")->setValue(pick_data->desc);
    setSnapshotId(pick_data->snapshot_id);
    setPosGlobal(pick_data->pos_global);
    setPickLocation(createLocationText(LLStringUtil::null, pick_data->name,
            pick_data->sim_name, pick_data->pos_global));
}

BOOL LLPanelPickEdit::postBuild()
{
    LLPanelPickInfo::postBuild();

    mSnapshotCtrl->setCommitCallback(boost::bind(&LLPanelPickEdit::onSnapshotChanged, this));

    LLLineEditor* line_edit = getChild<LLLineEditor>("pick_name");
    line_edit->setKeystrokeCallback(boost::bind(&LLPanelPickEdit::onPickChanged, this, _1), nullptr);

    LLTextEditor* text_edit = getChild<LLTextEditor>("pick_desc");
    text_edit->setKeystrokeCallback(boost::bind(&LLPanelPickEdit::onPickChanged, this, _1));

    childSetAction(XML_BTN_SAVE, boost::bind(&LLPanelPickEdit::onClickSave, this));
    childSetAction("set_to_curr_location_btn", boost::bind(&LLPanelPickEdit::onClickSetLocation, this));

    initTexturePickerMouseEvents();

    return TRUE;
}

void LLPanelPickEdit::setSaveCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("save_changes_btn")->setClickedCallback(cb);
}

void LLPanelPickEdit::setCancelCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("cancel_btn")->setClickedCallback(cb);
}

void LLPanelPickEdit::resetDirty()
{
    LLPanelPickInfo::resetDirty();

    getChild<LLLineEditor>("pick_name")->resetDirty();
    getChild<LLTextEditor>("pick_desc")->resetDirty();
    mSnapshotCtrl->resetDirty();
    mLocationChanged = false;
}

BOOL LLPanelPickEdit::isDirty() const
{
    if( mNewPick
        || LLPanelPickInfo::isDirty()
        || mLocationChanged
        || mSnapshotCtrl->isDirty()
        || getChild<LLLineEditor>("pick_name")->isDirty()
        || getChild<LLTextEditor>("pick_desc")->isDirty())
    {
        return TRUE;
    }
    return FALSE;
}

void LLPanelPickEdit::sendUpdate()
{
    LLPickData pick_data;

    // If we don't have a pick id yet, we'll need to generate one,
    // otherwise we'll keep overwriting pick_id 00000 in the database.
    if (getPickId().isNull())
    {
        getPickId().generate();
    }

    pick_data.agent_id = gAgent.getID();
    pick_data.session_id = gAgent.getSessionID();
    pick_data.pick_id = getPickId();
    pick_data.creator_id = gAgent.getID();;

    //legacy var  need to be deleted
    pick_data.top_pick = FALSE;
    pick_data.parcel_id = mParcelId;
    pick_data.name = getChild<LLUICtrl>(XML_NAME)->getValue().asString();
    pick_data.desc = getChild<LLUICtrl>(XML_DESC)->getValue().asString();
    pick_data.snapshot_id = mSnapshotCtrl->getImageAssetID();
    pick_data.pos_global = getPosGlobal();
    pick_data.sort_order = 0;
    pick_data.enabled = TRUE;

    LLAvatarPropertiesProcessor::instance().sendPickInfoUpdate(&pick_data);

    if(mNewPick)
    {
        // Assume a successful create pick operation, make new number of picks
        // available immediately. Actual number of picks will be requested in
        // LLAvatarPropertiesProcessor::sendPickInfoUpdate and updated upon server respond.
        LLAgentPicksInfo::getInstance()->incrementNumberOfPicks();
    }
}

void LLPanelPickEdit::onSnapshotChanged()
{
    enableSaveButton(true);
}

void LLPanelPickEdit::onPickChanged(LLUICtrl* ctrl)
{
    enableSaveButton(isDirty());
}

void LLPanelPickEdit::resetData()
{
    LLPanelPickInfo::resetData();
    mLocationChanged = false;
}

void LLPanelPickEdit::enableSaveButton(bool enable)
{
    getChildView(XML_BTN_SAVE)->setEnabled(enable);
}

void LLPanelPickEdit::onClickSetLocation()
{
    // Save location for later use.
    setPosGlobal(gAgent.getPositionGlobal());

    std::string parcel_name, region_name;

    LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
    if (parcel)
    {
        mParcelId = parcel->getID();
        parcel_name = parcel->getName();
    }

    LLViewerRegion* region = gAgent.getRegion();
    if(region)
    {
        region_name = region->getName();
    }

    setPickLocation(createLocationText(getLocationNotice(), parcel_name, region_name, getPosGlobal()));

    mLocationChanged = true;
    enableSaveButton(TRUE);
}

void LLPanelPickEdit::onClickSave()
{
    sendUpdate();

    mLocationChanged = false;

    LLSD params;
    params["action"] = "save_new_pick";
    notifyParent(params);
}

std::string LLPanelPickEdit::getLocationNotice()
{
    static std::string notice = getString("location_notice");
    return notice;
}

void LLPanelPickEdit::processProperties(void* data, EAvatarProcessorType type)
{
    if(mNeedData)
    {
        LLPanelPickInfo::processProperties(data, type);
    }
}

// PRIVATE AREA

void LLPanelPickEdit::initTexturePickerMouseEvents()
{
    text_icon = getChild<LLIconCtrl>(XML_BTN_ON_TXTR);
    mSnapshotCtrl->setMouseEnterCallback(boost::bind(&LLPanelPickEdit::onTexturePickerMouseEnter, this, _1));
    mSnapshotCtrl->setMouseLeaveCallback(boost::bind(&LLPanelPickEdit::onTexturePickerMouseLeave, this, _1));

    text_icon->setVisible(FALSE);
}

void LLPanelPickEdit::onTexturePickerMouseEnter(LLUICtrl* ctrl)
{
        text_icon->setVisible(TRUE);
}

void LLPanelPickEdit::onTexturePickerMouseLeave(LLUICtrl* ctrl)
{
    text_icon->setVisible(FALSE);
}
