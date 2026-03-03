/**
 * @file llinspectavatar.cpp
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#include "llinspectavatar.h"

// viewer files
#include "alavataractions.h"
#include "llagent.h"
#include "llagentdata.h"
#include "llavataractions.h"
#include "llavatariconctrl.h"
#include "llavatarnamecache.h"
#include "llavatarpropertiesprocessor.h"
#include "llcallingcard.h"
#include "lldateutil.h"
#include "llfloaterreporter.h"
#include "llfloaterworldmap.h"
#include "llimview.h"
#include "llinspect.h"
#include "llmutelist.h"
#include "llpanelblockedlist.h"
#include "llslurl.h"
#include "llstartup.h"
#include "llspeakers.h"
#include "llviewermenu.h"
#include "llvoiceclient.h"
#include "llviewerobjectlist.h"
#include "lltransientfloatermgr.h"
#include "llnotificationsutil.h"
#include "rlvactions.h"

// Linden libraries
#include "llfloater.h"
#include "llfloaterreg.h"
#include "llmenubutton.h"
#include "lltextbox.h"
#include "lltoggleablemenu.h"
#include "lltrans.h"
#include "lluictrl.h"

class LLFetchAvatarData;


//////////////////////////////////////////////////////////////////////////////
// LLInspectAvatar
//////////////////////////////////////////////////////////////////////////////

// Avatar Inspector, a small information window used when clicking
// on avatar names in the 2D UI and in the ambient inspector widget for
// the 3D world.
class LLInspectAvatar final : public LLInspect, LLTransientFloater
{
    friend class LLFloaterReg;

public:
    // avatar_id - Avatar ID for which to show information
    // Inspector will be positioned relative to current mouse position
    LLInspectAvatar(const LLSD& avatar_id);
    ~LLInspectAvatar() override;

    /*virtual*/ bool postBuild(void) override;

    // Because floater is single instance, need to re-parse data on each spawn
    // (for example, inspector about same avatar but in different position)
    /*virtual*/ void onOpen(const LLSD& avatar_id) override;

    // When closing they should close their gear menu
    /*virtual*/ void onClose(bool app_quitting) override;

    // Update view based on information from avatar properties processor
    void processAvatarData(LLAvatarData* data);

    // override the inspector mouse leave so timer is only paused if
    // gear menu is not open
    /* virtual */ void onMouseLeave(S32 x, S32 y, MASK mask) override;

    LLTransientFloaterMgr::ETransientGroup getGroup() override { return LLTransientFloaterMgr::GLOBAL; }

private:
    // Make network requests for all the data to display in this view.
    // Used on construction and if avatar id changes.
    void requestUpdate();

    // Set the volume slider to this user's current client-side volume setting,
    // hiding/disabling if the user is not nearby.
    void updateVolumeSlider();

    // Shows/hides moderator panel depending on voice state
    void updateModeratorPanel();

    // Moderator ability to enable/disable voice chat for avatar
    void toggleSelectedVoice(bool enabled);

    // Button callbacks
    void onClickAddFriend();
    void onClickRemoveFriend();
    void onClickViewProfile();
    void onClickIM();
    void onClickCall();
    void onClickTeleport();
    void onClickTeleportRequest();
    void onClickInviteToGroup();
    void onClickPay();
    void onClickShare();
    void onToggleMute();
    void onClickReport();
    void onClickFreeze();
    void onClickEject();
    void onClickEstateTPHome();
    void onClickEstateKick();
    void onClickEstateBan();
    void onClickGodFreeze();
    void onClickGodKick();
    void onClickCSR();
    void onClickZoomIn();
    void onClickFindOnMap();
    void onClickViewChatHistory();
    void onClickTeleportTo();
    bool onVisibleFindOnMap();
    bool onVisibleFreezeEject();
    bool onVisibleManageEstate();
    bool onVisibleZoomIn();
    bool onVisibleTeleportTo();
    bool onVisibleChatHistory();
    void onClickMuteVolume();
    void onVolumeChange(const LLSD& data);
    bool enableMute();
    bool enableUnmute();
    bool enableTeleportOffer();
    bool enableTeleportRequest();
    bool enablePay();
    bool godModeEnabled();

    // Is used to determine if "Add friend" option should be enabled in gear menu
    bool isFriend();
    bool isNotFriend();

    void moderationActionCoro(std::string url, LLSD action);

    void onAvatarNameCache(const LLUUID& agent_id,
                           const LLAvatarName& av_name);

private:
    LLUUID              mAvatarID;
    // Need avatar name information to spawn friend add request
    LLAvatarName        mAvatarName;
    // an in-flight request for avatar properties from LLAvatarPropertiesProcessor
    // is represented by this object
    LLFetchAvatarData*  mPropertiesRequest;
    boost::signals2::connection mAvatarNameCacheConnection;
};

//////////////////////////////////////////////////////////////////////////////
// LLFetchAvatarData
//////////////////////////////////////////////////////////////////////////////

// This object represents a pending request for avatar properties information
class LLFetchAvatarData : public LLAvatarPropertiesObserver
{
public:
    // If the inspector closes it will delete the pending request object, so the
    // inspector pointer will be valid for the lifetime of this object
    LLFetchAvatarData(const LLUUID& avatar_id, LLInspectAvatar* inspector)
    :   mAvatarID(avatar_id),
        mInspector(inspector)
    {
        LLAvatarPropertiesProcessor* processor =
            LLAvatarPropertiesProcessor::getInstance();
        // register ourselves as an observer
        processor->addObserver(mAvatarID, this);
        // send a request (duplicates will be suppressed inside the avatar
        // properties processor)
        processor->sendAvatarPropertiesRequest(mAvatarID);
    }

    ~LLFetchAvatarData()
    {
        // remove ourselves as an observer
        LLAvatarPropertiesProcessor::getInstance()->
        removeObserver(mAvatarID, this);
    }

    void processProperties(void* data, EAvatarProcessorType type)
    {
        // route the data to the inspector
        if (data
            && type == APT_PROPERTIES)
        {
            LLAvatarData* avatar_data = static_cast<LLAvatarData*>(data);
            mInspector->processAvatarData(avatar_data);
        }
    }

    // Store avatar ID so we can un-register the observer on destruction
    LLUUID mAvatarID;
    LLInspectAvatar* mInspector;
};

LLInspectAvatar::LLInspectAvatar(const LLSD& sd)
:   LLInspect( LLSD() ),    // single_instance, doesn't really need key
    mAvatarID(),            // set in onOpen()  *Note: we used to show partner's name but we dont anymore --angela 3rd Dec*
    mAvatarName(),
    mPropertiesRequest(NULL),
    mAvatarNameCacheConnection()
{
    mCommitCallbackRegistrar.add("InspectAvatar.ViewProfile",   boost::bind(&LLInspectAvatar::onClickViewProfile, this));
    mCommitCallbackRegistrar.add("InspectAvatar.AddFriend", boost::bind(&LLInspectAvatar::onClickAddFriend, this));
    mCommitCallbackRegistrar.add("InspectAvatar.RemoveFriend", boost::bind(&LLInspectAvatar::onClickRemoveFriend, this));
    mCommitCallbackRegistrar.add("InspectAvatar.IM",        boost::bind(&LLInspectAvatar::onClickIM, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Call",      boost::bind(&LLInspectAvatar::onClickCall, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Teleport",  boost::bind(&LLInspectAvatar::onClickTeleport, this));
    mCommitCallbackRegistrar.add("InspectAvatar.TeleportRequest",    boost::bind(&LLInspectAvatar::onClickTeleportRequest, this));
    mCommitCallbackRegistrar.add("InspectAvatar.InviteToGroup", boost::bind(&LLInspectAvatar::onClickInviteToGroup, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Pay",   boost::bind(&LLInspectAvatar::onClickPay, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Share", boost::bind(&LLInspectAvatar::onClickShare, this));
    mCommitCallbackRegistrar.add("InspectAvatar.ToggleMute",    boost::bind(&LLInspectAvatar::onToggleMute, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Freeze", boost::bind(&LLInspectAvatar::onClickFreeze, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Eject", boost::bind(&LLInspectAvatar::onClickEject, this));
    mCommitCallbackRegistrar.add("InspectAvatar.EstateTPHome", boost::bind(&LLInspectAvatar::onClickEstateTPHome, this));
    mCommitCallbackRegistrar.add("InspectAvatar.EstateKick", boost::bind(&LLInspectAvatar::onClickEstateKick, this));
    mCommitCallbackRegistrar.add("InspectAvatar.EstateBan", boost::bind(&LLInspectAvatar::onClickEstateBan, this));
    mCommitCallbackRegistrar.add("InspectAvatar.GodFreeze", boost::bind(&LLInspectAvatar::onClickGodFreeze, this));
    mCommitCallbackRegistrar.add("InspectAvatar.GodKick", boost::bind(&LLInspectAvatar::onClickGodKick, this));
    mCommitCallbackRegistrar.add("InspectAvatar.CSR", boost::bind(&LLInspectAvatar::onClickCSR, this));
    mCommitCallbackRegistrar.add("InspectAvatar.Report",    boost::bind(&LLInspectAvatar::onClickReport, this));
    mCommitCallbackRegistrar.add("InspectAvatar.FindOnMap", boost::bind(&LLInspectAvatar::onClickFindOnMap, this));
    mCommitCallbackRegistrar.add("InspectAvatar.ZoomIn", boost::bind(&LLInspectAvatar::onClickZoomIn, this));
    mCommitCallbackRegistrar.add("InspectAvatar.DisableVoice", boost::bind(&LLInspectAvatar::toggleSelectedVoice, this, false));
    mCommitCallbackRegistrar.add("InspectAvatar.EnableVoice", boost::bind(&LLInspectAvatar::toggleSelectedVoice, this, true));
    mCommitCallbackRegistrar.add("InspectAvatar.ViewChatHistory", boost::bind(&LLInspectAvatar::onClickViewChatHistory, this));
    mCommitCallbackRegistrar.add("InspectAvatar.TeleportTo", boost::bind(&LLInspectAvatar::onClickTeleportTo, this));

    mEnableCallbackRegistrar.add("InspectAvatar.EnableGod", boost::bind(&LLInspectAvatar::godModeEnabled, this));
    mEnableCallbackRegistrar.add("InspectAvatar.VisibleFindOnMap",  boost::bind(&LLInspectAvatar::onVisibleFindOnMap, this));
    mEnableCallbackRegistrar.add("InspectAvatar.VisibleFreezeEject",  boost::bind(&LLInspectAvatar::onVisibleFreezeEject, this));
    mEnableCallbackRegistrar.add("InspectAvatar.VisibleManageEstate", boost::bind(&LLInspectAvatar::onVisibleManageEstate, this));
    mEnableCallbackRegistrar.add("InspectAvatar.VisibleZoomIn", boost::bind(&LLInspectAvatar::onVisibleZoomIn, this));
    mEnableCallbackRegistrar.add("InspectAvatar.VisibleTeleportTo", boost::bind(&LLInspectAvatar::onVisibleTeleportTo, this));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnableRemoveFriend", boost::bind(&LLInspectAvatar::isFriend, this));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnableAddFriend", boost::bind(&LLInspectAvatar::isNotFriend, this));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnableCall", boost::bind(&LLAvatarActions::canCall));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnableChatHistory", boost::bind(&LLInspectAvatar::onVisibleChatHistory, this));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnableTeleportOffer", boost::bind(&LLInspectAvatar::enableTeleportOffer, this));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnableTeleportRequest",    boost::bind(&LLInspectAvatar::enableTeleportRequest, this));
    mEnableCallbackRegistrar.add("InspectAvatar.Gear.EnablePay",    boost::bind(&LLInspectAvatar::enablePay, this));
    mEnableCallbackRegistrar.add("InspectAvatar.EnableMute", boost::bind(&LLInspectAvatar::enableMute, this));
    mEnableCallbackRegistrar.add("InspectAvatar.EnableUnmute", boost::bind(&LLInspectAvatar::enableUnmute, this));

    // can't make the properties request until the widgets are constructed
    // as it might return immediately, so do it in onOpen.

    LLTransientFloaterMgr::getInstance()->addControlView(LLTransientFloaterMgr::GLOBAL, this);
    LLTransientFloater::init(this);
}

LLInspectAvatar::~LLInspectAvatar()
{
    if (mAvatarNameCacheConnection.connected())
    {
        mAvatarNameCacheConnection.disconnect();
    }
    // clean up any pending requests so they don't call back into a deleted
    // view
    delete mPropertiesRequest;
    mPropertiesRequest = NULL;

    LLTransientFloaterMgr::getInstance()->removeControlView(this);
}

/*virtual*/
bool LLInspectAvatar::postBuild(void)
{
    getChild<LLUICtrl>("add_friend_btn")->setCommitCallback(
        boost::bind(&LLInspectAvatar::onClickAddFriend, this) );

    getChild<LLUICtrl>("view_profile_btn")->setCommitCallback(
        boost::bind(&LLInspectAvatar::onClickViewProfile, this) );

    getChild<LLUICtrl>("mute_btn")->setCommitCallback(
        boost::bind(&LLInspectAvatar::onClickMuteVolume, this) );

    getChild<LLUICtrl>("volume_slider")->setCommitCallback(
        boost::bind(&LLInspectAvatar::onVolumeChange, this, _2));

    return true;
}


// Multiple calls to showInstance("inspect_avatar", foo) will provide different
// LLSD for foo, which we will catch here.
//virtual
void LLInspectAvatar::onOpen(const LLSD& data)
{
    // Start open animation
    LLInspect::onOpen(data);

    // Extract appropriate avatar id
    mAvatarID = data["avatar_id"];

    bool self = mAvatarID == gAgent.getID();

    getChild<LLUICtrl>("gear_self_btn")->setVisible(self);
    getChild<LLUICtrl>("gear_btn")->setVisible(!self);

    LLInspect::repositionInspector(data);

    // can't call from constructor as widgets are not built yet
    requestUpdate();

    updateVolumeSlider();

    updateModeratorPanel();
}

// virtual
void LLInspectAvatar::onClose(bool app_quitting)
{
    getChild<LLMenuButton>("gear_btn")->hideMenu();
    getChild<LLMenuButton>("gear_self_btn")->hideMenu();
}

void LLInspectAvatar::requestUpdate()
{
    // Don't make network requests when spawning from the debug menu at the
    // login screen (which is useful to work on the layout).
    if (mAvatarID.isNull())
    {
        if (LLStartUp::getStartupState() >= STATE_STARTED)
        {
            // once we're running we don't want to show the test floater
            // for bogus LLUUID::null links
            closeFloater();
        }
        return;
    }

    // Clear out old data so it doesn't flash between old and new
    getChild<LLUICtrl>("user_name")->setValue("");
    getChild<LLUICtrl>("user_name_small")->setValue("");
    getChild<LLUICtrl>("user_slid")->setValue("");
    getChild<LLUICtrl>("user_subtitle")->setValue("");
    getChild<LLUICtrl>("user_details")->setValue("");

    // Make a new request for properties
    delete mPropertiesRequest;
    mPropertiesRequest = new LLFetchAvatarData(mAvatarID, this);

    // You can't re-add someone as a friend if they are already your friend
    bool is_friend = LLAvatarTracker::instance().getBuddyInfo(mAvatarID) != NULL;
    bool is_self = (mAvatarID == gAgentID);
    if (is_self)
    {
        getChild<LLUICtrl>("add_friend_btn")->setVisible(false);
        getChild<LLUICtrl>("im_btn")->setVisible(false);
    }
    else if (is_friend)
    {
        getChild<LLUICtrl>("add_friend_btn")->setVisible(false);
        getChild<LLUICtrl>("im_btn")->setVisible(true);
    }
    else
    {
        getChild<LLUICtrl>("add_friend_btn")->setVisible(true);
        getChild<LLUICtrl>("im_btn")->setVisible(false);
    }

    // Use an avatar_icon even though the image id will come down with the
    // avatar properties because the avatar_icon code maintains a cache of icons
    // and this may result in the image being visible sooner.
    // *NOTE: This may generate a duplicate avatar properties request, but that
    // will be suppressed internally in the avatar properties processor.

    //remove avatar id from cache to get fresh info
    LLAvatarIconIDCache::getInstance()->remove(mAvatarID);

    getChild<LLUICtrl>("avatar_icon")->setValue(LLSD(mAvatarID) );

    if (mAvatarNameCacheConnection.connected())
    {
        mAvatarNameCacheConnection.disconnect();
    }
    mAvatarNameCacheConnection = LLAvatarNameCache::get(mAvatarID,boost::bind(&LLInspectAvatar::onAvatarNameCache,this, _1, _2));
}

void LLInspectAvatar::processAvatarData(LLAvatarData* data)
{
    LLStringUtil::format_map_t args;

    std::string birth_date = LLTrans::getString(data->hide_age ?
        "AvatarBirthDateFormatShort" :
        "AvatarBirthDateFormatFull");
        LLStringUtil::format(birth_date, LLSD().with("datetime", (S32) data->born_on.secondsSinceEpoch()));
        args["[BORN_ON]"] = birth_date;
    args["[AGE]"] = data->hide_age ?
        LLStringUtilBase<char>::null :
        LLDateUtil::ageFromDate(data->born_on, LLDate::now());
    args["[SL_PROFILE]"] = data->about_text;
    args["[RW_PROFILE"] = data->fl_about_text;
    args["[ACCTTYPE]"] = LLAvatarPropertiesProcessor::accountType(data);
    std::string payment_info = LLAvatarPropertiesProcessor::paymentInfo(data);
    args["[PAYMENTINFO]"] = payment_info;
    args["[COMMA]"] = (payment_info.empty() ? "" : ",");

    std::string subtitle = getString("Subtitle", args);
    getChild<LLUICtrl>("user_subtitle")->setValue( LLSD(subtitle) );
    std::string details = getString("Details", args);
    getChild<LLUICtrl>("user_details")->setValue( LLSD(details) );

    // Delete the request object as it has been satisfied
    delete mPropertiesRequest;
    mPropertiesRequest = NULL;
}

// For the avatar inspector, we only want to unpause the fade timer
// if neither the gear menu or self gear menu are open
void LLInspectAvatar::onMouseLeave(S32 x, S32 y, MASK mask)
{
    LLToggleableMenu* gear_menu = getChild<LLMenuButton>("gear_btn")->getMenu();
    LLToggleableMenu* gear_menu_self = getChild<LLMenuButton>("gear_self_btn")->getMenu();
    if ( gear_menu && gear_menu->getVisible() &&
         gear_menu_self && gear_menu_self->getVisible() )
    {
        return;
    }

    if(childHasVisiblePopupMenu())
    {
        return;
    }

    mOpenTimer.unpause();
}

void LLInspectAvatar::updateModeratorPanel()
{
    bool enable_moderator_panel = false;

    if (LLVoiceChannel::getCurrentVoiceChannel() &&
        mAvatarID != gAgent.getID())
    {
        LLUUID session_id = LLVoiceChannel::getCurrentVoiceChannel()->getSessionID();

        if (session_id != LLUUID::null)
        {
            LLIMSpeakerMgr* speaker_mgr = LLIMModel::getInstance()->getSpeakerManager(session_id);

            if (speaker_mgr)
            {
                LLPointer<LLSpeaker> self_speakerp = speaker_mgr->findSpeaker(gAgent.getID());
                LLPointer<LLSpeaker> selected_speakerp = speaker_mgr->findSpeaker(mAvatarID);

                if(speaker_mgr->isVoiceActive() && selected_speakerp &&
                    selected_speakerp->isInVoiceChannel() &&
                    ((self_speakerp && self_speakerp->mIsModerator) || gAgent.isGodlike()))
                {
                    getChild<LLUICtrl>("enable_voice")->setVisible(selected_speakerp->mModeratorMutedVoice);
                    getChild<LLUICtrl>("disable_voice")->setVisible(!selected_speakerp->mModeratorMutedVoice);

                    enable_moderator_panel = true;
                }
            }
        }
    }

    if (enable_moderator_panel)
    {
        if (!getChild<LLUICtrl>("moderator_panel")->getVisible())
        {
            getChild<LLUICtrl>("moderator_panel")->setVisible(true);
            // stretch the floater so it can accommodate the moderator panel
            reshape(getRect().getWidth(), getRect().getHeight() + getChild<LLUICtrl>("moderator_panel")->getRect().getHeight());
        }
    }
    else if (getChild<LLUICtrl>("moderator_panel")->getVisible())
    {
        getChild<LLUICtrl>("moderator_panel")->setVisible(false);
        // shrink the inspector floater back to original size
        reshape(getRect().getWidth(), getRect().getHeight() - getChild<LLUICtrl>("moderator_panel")->getRect().getHeight());
    }
}

void LLInspectAvatar::toggleSelectedVoice(bool enabled)
{
    LLUUID session_id = LLVoiceChannel::getCurrentVoiceChannel()->getSessionID();
    LLIMSpeakerMgr* speaker_mgr = LLIMModel::getInstance()->getSpeakerManager(session_id);

    if (speaker_mgr)
    {
        std::string url = gAgent.getRegionCapability("ChatSessionRequest");
        if (!url.empty())
        {
            LLSD data;
            data["method"] = "mute update";
            data["session-id"] = session_id;
            data["params"] = LLSD::emptyMap();
            data["params"]["agent_id"] = mAvatarID;
            data["params"]["mute_info"] = LLSD::emptyMap();
            // ctrl value represents ability to type, so invert
            data["params"]["mute_info"]["voice"] = !enabled;

            LLCoros::instance().launch("LLIMSpeakerMgr::moderationActionCoro",
                boost::bind(&LLInspectAvatar::moderationActionCoro, this, url, data));
        }
    }

    closeFloater();
}

void LLInspectAvatar::updateVolumeSlider()
{
    bool voice_enabled = LLVoiceClient::getInstance()->getVoiceEnabled(mAvatarID);

    // Do not display volume slider and mute button if it
    // is ourself or we are not in a voice channel together
    if (!voice_enabled || (mAvatarID == gAgent.getID()))
    {
        getChild<LLUICtrl>("mute_btn")->setVisible(false);
        getChild<LLUICtrl>("volume_slider")->setVisible(false);
    }

    else
    {
        getChild<LLUICtrl>("mute_btn")->setVisible(true);
        getChild<LLUICtrl>("volume_slider")->setVisible(true);

        // By convention, we only display and toggle voice mutes, not all mutes
        bool is_muted = LLAvatarActions::isVoiceMuted(mAvatarID);

        LLUICtrl* mute_btn = getChild<LLUICtrl>("mute_btn");

        bool is_linden = LLStringUtil::endsWith(mAvatarName.getDisplayName(), " Linden");

        mute_btn->setEnabled( !is_linden);
        mute_btn->setValue( is_muted );

        LLUICtrl* volume_slider = getChild<LLUICtrl>("volume_slider");
        volume_slider->setEnabled( !is_muted );

        F32 volume;

        if (is_muted)
        {
            // it's clearer to display their volume as zero
            volume = 0.f;
        }
        else
        {
            // actual volume
            volume = LLVoiceClient::getInstance()->getUserVolume(mAvatarID);
        }
        volume_slider->setValue( (F64)volume );
    }

}

void LLInspectAvatar::onClickMuteVolume()
{
    // By convention, we only display and toggle voice mutes, not all mutes
    LLMuteList* mute_list = LLMuteList::getInstance();
    bool is_muted = mute_list->isMuted(mAvatarID, LLMute::flagVoiceChat);

    LLMute mute(mAvatarID, mAvatarName.getUserName(), LLMute::AGENT);
    if (!is_muted)
    {
        mute_list->add(mute, LLMute::flagVoiceChat);
    }
    else
    {
        mute_list->remove(mute, LLMute::flagVoiceChat);
    }

    updateVolumeSlider();
}

void LLInspectAvatar::onVolumeChange(const LLSD& data)
{
    F32 volume = (F32)data.asReal();
    LLVoiceClient::getInstance()->setUserVolume(mAvatarID, volume);
}

void LLInspectAvatar::onAvatarNameCache(
        const LLUUID& agent_id,
        const LLAvatarName& av_name)
{
    mAvatarNameCacheConnection.disconnect();

    if (agent_id == mAvatarID)
    {
        getChild<LLUICtrl>("user_name")->setValue(av_name.getDisplayName());
        getChild<LLUICtrl>("user_name_small")->setValue(av_name.getDisplayName());
        getChild<LLUICtrl>("user_slid")->setValue(av_name.getUserName());
        mAvatarName = av_name;

        // show smaller display name if too long to display in regular size
        if (getChild<LLTextBox>("user_name")->getTextPixelWidth() > getChild<LLTextBox>("user_name")->getRect().getWidth())
        {
            getChild<LLUICtrl>("user_name_small")->setVisible( true );
            getChild<LLUICtrl>("user_name")->setVisible( false );
        }
        else
        {
            getChild<LLUICtrl>("user_name_small")->setVisible( false );
            getChild<LLUICtrl>("user_name")->setVisible( true );

        }

    }
}

void LLInspectAvatar::onClickAddFriend()
{
    LLAvatarActions::requestFriendshipDialog(mAvatarID, mAvatarName.getDisplayName());
    closeFloater();
}

void LLInspectAvatar::onClickRemoveFriend()
{
    LLAvatarActions::removeFriendDialog(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickViewProfile()
{
    LLAvatarActions::showProfile(mAvatarID);
    closeFloater();
}

bool LLInspectAvatar::isFriend()
{
    return LLAvatarActions::isFriend(mAvatarID);
}

bool LLInspectAvatar::isNotFriend()
{
    return !LLAvatarActions::isFriend(mAvatarID);
}

bool LLInspectAvatar::onVisibleFindOnMap()
{
    return ALAvatarActions::isAgentMappable(mAvatarID);
}

bool LLInspectAvatar::onVisibleFreezeEject()
{
    return ALAvatarActions::canFreezeEject(mAvatarID);
}

bool LLInspectAvatar::onVisibleManageEstate()
{
    return ALAvatarActions::canManageAvatarsEstate(mAvatarID);
}

bool LLInspectAvatar::onVisibleZoomIn()
{
    return ALAvatarActions::canZoomIn(mAvatarID);
}

bool LLInspectAvatar::onVisibleTeleportTo()
{
    return ALAvatarActions::canTeleportTo(mAvatarID);
}

bool LLInspectAvatar::onVisibleChatHistory()
{
    return LLLogChat::isTranscriptExist(mAvatarID);
}

void LLInspectAvatar::onClickIM()
{
    LLAvatarActions::startIM(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickCall()
{
    LLAvatarActions::startCall(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickTeleport()
{
    LLAvatarActions::offerTeleport(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickTeleportRequest()
{
    LLAvatarActions::teleportRequest(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickInviteToGroup()
{
    LLAvatarActions::inviteToGroup(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickPay()
{
    LLAvatarActions::pay(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickShare()
{
    LLAvatarActions::share(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onToggleMute()
{
    LLMute mute(mAvatarID, mAvatarName.getUserName(), LLMute::AGENT);

    if (LLMuteList::getInstance()->isMuted(mute.mID, mute.mName))
    {
        LLMuteList::getInstance()->remove(mute);
    }
    else
    {
        LLMuteList::getInstance()->add(mute);
    }

    LLPanelBlockedList::showPanelAndSelect(mute.mID);
    closeFloater();
}

void LLInspectAvatar::onClickReport()
{
    LLFloaterReporter::showFromAvatar(mAvatarID, mAvatarName.getCompleteName());
    closeFloater();
}

void LLInspectAvatar::onClickFreeze()
{
    // use default "local" version of freezing that requires avatar to be in range
    ALAvatarActions::parcelFreeze(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickEject()
{
    ALAvatarActions::parcelEject(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickEstateTPHome()
{
    ALAvatarActions::estateTeleportHome(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickEstateKick()
{
    ALAvatarActions::estateKick(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickEstateBan()
{
    ALAvatarActions::estateBan(mAvatarID);
    closeFloater();
}

bool godlike_freeze(const LLSD& notification, const LLSD& response)
{
    LLUUID avatar_id = notification["payload"]["avatar_id"].asUUID();
    S32    option    = LLNotificationsUtil::getSelectedOption(notification, response);

    switch (option)
    {
        case 0:
            ALAvatarActions::godFreeze(avatar_id);
            break;
        case 1:
            ALAvatarActions::godUnfreeze(avatar_id);
            break;
        default:
            break;
    }

    return false;
}

void LLInspectAvatar::onClickGodFreeze()
{
    if (gAgent.isGodlike())
    {
        // use godlike freeze-at-a-distance, with confirmation
        LLNotificationsUtil::add("FreezeAvatar", LLSD(), LLSD().with("avatar_id", mAvatarID), godlike_freeze);
    }
    closeFloater();
}

void LLInspectAvatar::onClickGodKick()
{
    ALAvatarActions::godKick(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickCSR()
{
    LLAvatarName av_name;
    LLAvatarNameCache::get(mAvatarID, &av_name);
    std::string name = av_name.getUserName();
    LLAvatarActions::csr(mAvatarID, name);
    closeFloater();
}

void LLInspectAvatar::onClickZoomIn()
{
    ALAvatarActions::zoomIn(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickFindOnMap()
{
    gFloaterWorldMap->trackAvatar(mAvatarID, mAvatarName.getDisplayName());
    LLFloaterReg::showInstance("world_map");
}

void LLInspectAvatar::onClickViewChatHistory()
{
    LLAvatarActions::viewChatHistory(mAvatarID);
    closeFloater();
}

void LLInspectAvatar::onClickTeleportTo()
{
    ALAvatarActions::teleportTo(mAvatarID);
    closeFloater();
}

bool LLInspectAvatar::enableMute()
{
    bool is_linden = LLStringUtil::endsWith(mAvatarName.getDisplayName(), " Linden");
    bool is_self = mAvatarID == gAgent.getID();

    if (!is_linden && !is_self && !LLMuteList::getInstance()->isMuted(mAvatarID, mAvatarName.getDisplayName()))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool LLInspectAvatar::enableUnmute()
{
    bool is_linden = LLStringUtil::endsWith(mAvatarName.getDisplayName(), " Linden");
    bool is_self = mAvatarID == gAgent.getID();

    if (!is_linden && !is_self && LLMuteList::getInstance()->isMuted(mAvatarID, mAvatarName.getDisplayName()))
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool LLInspectAvatar::enableTeleportOffer()
{
    return LLAvatarActions::canOfferTeleport(mAvatarID);
}

bool LLInspectAvatar::enableTeleportRequest()
{
    if (LLAvatarTracker::instance().isBuddy(mAvatarID))
    {
        return LLAvatarTracker::instance().isBuddyOnline(mAvatarID);
    }
    return false;
}

bool LLInspectAvatar::enablePay()
{
    return RlvActions::canPayAvatar(mAvatarID);
}

bool LLInspectAvatar::godModeEnabled()
{
    return gAgent.isGodlike();
}

void LLInspectAvatar::moderationActionCoro(std::string url, LLSD action)
{
    LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter(new LLCoreHttpUtil::HttpCoroutineAdapter("moderationActionCoro", httpPolicy));
    LLCore::HttpRequest::ptr_t httpRequest(new LLCore::HttpRequest);
    LLCore::HttpOptions::ptr_t httpOpts = LLCore::HttpOptions::ptr_t(new LLCore::HttpOptions);

    httpOpts->setWantHeaders(true);

    LLUUID sessionId = action["session-id"];

    LLSD result = httpAdapter->postAndSuspend(httpRequest, url, action, httpOpts);
    LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
    LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

    if (!status)
    {
        if (gIMMgr)
        {
            //403 == you're not a mod
            //should be disabled if you're not a moderator
            if (status == LLCore::HttpStatus(HTTP_FORBIDDEN))
            {
                gIMMgr->showSessionEventError(
                    "mute",
                    "not_a_mod_error",
                    sessionId);
            }
            else
            {
                gIMMgr->showSessionEventError(
                    "mute",
                    "generic_request_error",
                    sessionId);
            }
        }
    }
}

//////////////////////////////////////////////////////////////////////////////
// LLInspectAvatarUtil
//////////////////////////////////////////////////////////////////////////////
void LLInspectAvatarUtil::registerFloater()
{
    LLFloaterReg::add("inspect_avatar", "inspect_avatar.xml",
                      &LLFloaterReg::build<LLInspectAvatar>);
}
