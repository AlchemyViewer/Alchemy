/**
 * @file alpanelpick.cpp
 * @brief ALPanelPick class implementation
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

#include "alpanelpick.h"

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

static const std::string XML_PANEL_EDIT_PICK("panel_al_edit_pick.xml");
static const std::string XML_PANEL_PICK_INFO("panel_al_pick_info.xml");

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
ALPanelPickInfo* ALPanelPickInfo::create()
{
    ALPanelPickInfo* panel = new ALPanelPickInfo();
    panel->buildFromFile(XML_PANEL_PICK_INFO);
    return panel;
}

ALPanelPickInfo::ALPanelPickInfo()
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

ALPanelPickInfo::~ALPanelPickInfo()
{
    LLAvatarPropertiesProcessor::getInstance()->removeObserver(ALPanelPickInfo::getAvatarId(), this);

    if (mParcelId.notNull())
    {
        LLRemoteParcelInfoProcessor::getInstance()->removeObserver(mParcelId, this);
    }
}

void ALPanelPickInfo::onOpen(const LLSD& key)
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

bool ALPanelPickInfo::postBuild()
{
    mSnapshotCtrl = getChild<LLTextureCtrl>(XML_SNAPSHOT);

    childSetAction("teleport_btn", boost::bind(&ALPanelPickInfo::onClickTeleport, this));
    childSetAction("show_on_map_btn", boost::bind(&ALPanelPickInfo::onClickMap, this));
    childSetAction("back_btn", boost::bind(&ALPanelPickInfo::onClickBack, this));

    mScrollingPanel = getChild<LLPanel>("scroll_content_panel");
    mScrollContainer = getChild<LLScrollContainer>("profile_scroll");

    mScrollingPanelMinHeight = mScrollContainer->getScrolledViewRect().getHeight();
    mScrollingPanelWidth = mScrollingPanel->getRect().getWidth();

    LLTextEditor* text_desc = getChild<LLTextEditor>(XML_DESC);
    text_desc->setContentTrusted(false);

    return true;
}

void ALPanelPickInfo::reshape(S32 width, S32 height, bool called_from_parent)
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

void ALPanelPickInfo::processProperties(void* data, EAvatarProcessorType type)
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

void ALPanelPickInfo::sendParcelInfoRequest()
{
    if (mParcelId != mRequestedId)
    {
        LLRemoteParcelInfoProcessor::getInstance()->addObserver(mParcelId, this);
        LLRemoteParcelInfoProcessor::getInstance()->sendParcelInfoRequest(mParcelId);

        mRequestedId = mParcelId;
    }
}

void ALPanelPickInfo::setExitCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("back_btn")->setClickedCallback(cb);
}

void ALPanelPickInfo::processParcelInfo(const LLParcelData& parcel_data)
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

void ALPanelPickInfo::setEditPickCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("edit_btn")->setClickedCallback(cb);
}

// PROTECTED AREA

void ALPanelPickInfo::resetControls()
{
    if(getAvatarId() == gAgent.getID())
    {
        getChildView("edit_btn")->setEnabled(true);
        getChildView("edit_btn")->setVisible(true);
    }
    else
    {
        getChildView("edit_btn")->setEnabled(false);
        getChildView("edit_btn")->setVisible(false);
    }
}

void ALPanelPickInfo::resetData()
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
std::string ALPanelPickInfo::createLocationText(const std::string& owner_name, const std::string& original_name, const std::string& sim_name, const LLVector3d& pos_global)
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

void ALPanelPickInfo::setSnapshotId(const LLUUID& id)
{
    mSnapshotCtrl->setImageAssetID(id);
    mSnapshotCtrl->setValid(true);
}

void ALPanelPickInfo::setPickName(const std::string& name)
{
    getChild<LLUICtrl>(XML_NAME)->setValue(name);
}

void ALPanelPickInfo::setPickDesc(const std::string& desc)
{
    getChild<LLUICtrl>(XML_DESC)->setValue(desc);
}

void ALPanelPickInfo::setPickLocation(const std::string& location)
{
    getChild<LLUICtrl>(XML_LOCATION)->setValue(location);
}

void ALPanelPickInfo::onClickMap()
{
    LLFloaterWorldMap::getInstance()->trackLocation(getPosGlobal());
    LLFloaterReg::showInstance("world_map", "center");
}

void ALPanelPickInfo::onClickTeleport()
{
    if (!getPosGlobal().isExactlyZero())
    {
        gAgent.teleportViaLocation(getPosGlobal());
        LLFloaterWorldMap::getInstance()->trackLocation(getPosGlobal());
    }
}

void ALPanelPickInfo::onClickBack()
{
    LLAvatarPropertiesProcessor::getInstance()->removeObserver(getAvatarId(), this);
}

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////

//static
ALPanelPickEdit* ALPanelPickEdit::create()
{
    ALPanelPickEdit* panel = new ALPanelPickEdit();
    panel->buildFromFile(XML_PANEL_EDIT_PICK);
    return panel;
}

ALPanelPickEdit::ALPanelPickEdit()
 : ALPanelPickInfo()
 , mLocationChanged(false)
 , mNeedData(true)
 , mNewPick(false)
 , text_icon(nullptr)
{
}

void ALPanelPickEdit::onOpen(const LLSD& key)
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
        ALPanelPickInfo::onOpen(key);

        enableSaveButton(false);
    }

    resetDirty();
}

void ALPanelPickEdit::setPickData(const LLPickData* pick_data)
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

bool ALPanelPickEdit::postBuild()
{
    ALPanelPickInfo::postBuild();

    mSnapshotCtrl->setCommitCallback(boost::bind(&ALPanelPickEdit::onSnapshotChanged, this));

    LLLineEditor* line_edit = getChild<LLLineEditor>("pick_name");
    line_edit->setKeystrokeCallback(boost::bind(&ALPanelPickEdit::onPickChanged, this, _1), nullptr);

    LLTextEditor* text_edit = getChild<LLTextEditor>("pick_desc");
    text_edit->setKeystrokeCallback(boost::bind(&ALPanelPickEdit::onPickChanged, this, _1));

    childSetAction(XML_BTN_SAVE, boost::bind(&ALPanelPickEdit::onClickSave, this));
    childSetAction("set_to_curr_location_btn", boost::bind(&ALPanelPickEdit::onClickSetLocation, this));

    initTexturePickerMouseEvents();

    return true;
}

void ALPanelPickEdit::setSaveCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("save_changes_btn")->setClickedCallback(cb);
}

void ALPanelPickEdit::setCancelCallback(const commit_callback_t& cb)
{
    getChild<LLButton>("cancel_btn")->setClickedCallback(cb);
}

void ALPanelPickEdit::resetDirty()
{
    ALPanelPickInfo::resetDirty();

    getChild<LLLineEditor>("pick_name")->resetDirty();
    getChild<LLTextEditor>("pick_desc")->resetDirty();
    mSnapshotCtrl->resetDirty();
    mLocationChanged = false;
}

bool ALPanelPickEdit::isDirty() const
{
    if( mNewPick
        || ALPanelPickInfo::isDirty()
        || mLocationChanged
        || mSnapshotCtrl->isDirty()
        || getChild<LLLineEditor>("pick_name")->isDirty()
        || getChild<LLTextEditor>("pick_desc")->isDirty())
    {
        return true;
    }
    return false;
}

void ALPanelPickEdit::sendUpdate()
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
    pick_data.top_pick = false;
    pick_data.parcel_id = mParcelId;
    pick_data.name = getChild<LLUICtrl>(XML_NAME)->getValue().asString();
    pick_data.desc = getChild<LLUICtrl>(XML_DESC)->getValue().asString();
    pick_data.snapshot_id = mSnapshotCtrl->getImageAssetID();
    pick_data.pos_global = getPosGlobal();
    pick_data.sort_order = 0;
    pick_data.enabled = true;

    LLAvatarPropertiesProcessor::instance().sendPickInfoUpdate(&pick_data);

    if(mNewPick)
    {
        // Assume a successful create pick operation, make new number of picks
        // available immediately. Actual number of picks will be requested in
        // LLAvatarPropertiesProcessor::sendPickInfoUpdate and updated upon server respond.
        LLAgentPicksInfo::getInstance()->incrementNumberOfPicks();
    }
}

void ALPanelPickEdit::onSnapshotChanged()
{
    enableSaveButton(true);
}

void ALPanelPickEdit::onPickChanged(LLUICtrl* ctrl)
{
    enableSaveButton(isDirty());
}

void ALPanelPickEdit::resetData()
{
    ALPanelPickInfo::resetData();
    mLocationChanged = false;
}

void ALPanelPickEdit::enableSaveButton(bool enable)
{
    getChildView(XML_BTN_SAVE)->setEnabled(enable);
}

void ALPanelPickEdit::onClickSetLocation()
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
    enableSaveButton(true);
}

void ALPanelPickEdit::onClickSave()
{
    sendUpdate();

    mLocationChanged = false;

    LLSD params;
    params["action"] = "save_new_pick";
    notifyParent(params);
}

std::string ALPanelPickEdit::getLocationNotice()
{
    static std::string notice = getString("location_notice");
    return notice;
}

void ALPanelPickEdit::processProperties(void* data, EAvatarProcessorType type)
{
    if(mNeedData)
    {
        ALPanelPickInfo::processProperties(data, type);
    }
}

// PRIVATE AREA

void ALPanelPickEdit::initTexturePickerMouseEvents()
{
    text_icon = getChild<LLIconCtrl>(XML_BTN_ON_TXTR);
    mSnapshotCtrl->setMouseEnterCallback(boost::bind(&ALPanelPickEdit::onTexturePickerMouseEnter, this, _1));
    mSnapshotCtrl->setMouseLeaveCallback(boost::bind(&ALPanelPickEdit::onTexturePickerMouseLeave, this, _1));

    text_icon->setVisible(false);
}

void ALPanelPickEdit::onTexturePickerMouseEnter(LLUICtrl* ctrl)
{
        text_icon->setVisible(true);
}

void ALPanelPickEdit::onTexturePickerMouseLeave(LLUICtrl* ctrl)
{
    text_icon->setVisible(false);
}
