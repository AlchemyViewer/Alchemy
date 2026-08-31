/**
 * @file llpaneltopinfobar.cpp
 * @brief Coordinates and Parcel Settings information panel definition
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
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

#include <fmt/format.h>

#include "llpaneltopinfobar.h"

#include "llagent.h"
#include "llagentui.h"
#include "llclipboard.h"
#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "lllandmarkactions.h"
#include "lllocationinputctrl.h"
#include "llnotificationsutil.h"
#include "llparcel.h"
#include "llslurl.h"
#include "lltrans.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
#include "rlvhandler.h"
// [/RLVa:KB]

class LLPanelTopInfoBar::LLParcelChangeObserver : public LLParcelObserver
{
public:
    LLParcelChangeObserver(LLPanelTopInfoBar* topInfoBar) : mTopInfoBar(topInfoBar) {}

private:
    /*virtual*/ void changed()
    {
        if (mTopInfoBar)
        {
            // The whole readout, not just the icons. The parcel's name is half
            // of what the location text says, and a rename used to reach it
            // only because the text was rebuilt on every frame anyway.
            mTopInfoBar->update();
        }
    }

    LLPanelTopInfoBar* mTopInfoBar;
};

LLPanelTopInfoBar::LLPanelTopInfoBar(): mParcelChangedObserver(0)
{
    buildFromFile( "panel_topinfo_bar.xml");
}

LLPanelTopInfoBar::~LLPanelTopInfoBar()
{
    if (mParcelChangedObserver)
    {
        LLViewerParcelMgr::getInstance()->removeObserver(mParcelChangedObserver);
        delete mParcelChangedObserver;
    }

    if (mParcelPropsCtrlConnection.connected())
    {
        mParcelPropsCtrlConnection.disconnect();
    }

    if (mParcelMgrConnection.connected())
    {
        mParcelMgrConnection.disconnect();
    }

    if (mShowCoordsCtrlConnection.connected())
    {
        mShowCoordsCtrlConnection.disconnect();
    }

    mRegionInfoConnection.disconnect();
    mHealthConnection.disconnect();
}

void LLPanelTopInfoBar::initParcelIcons()
{
    mParcelIcon[VOICE_ICON] = getChild<LLIconCtrl>("voice_icon");
    mParcelIcon[FLY_ICON] = getChild<LLIconCtrl>("fly_icon");
    mParcelIcon[PUSH_ICON] = getChild<LLIconCtrl>("push_icon");
    mParcelIcon[BUILD_ICON] = getChild<LLIconCtrl>("build_icon");
    mParcelIcon[SCRIPTS_ICON] = getChild<LLIconCtrl>("scripts_icon");
    mParcelIcon[DAMAGE_ICON] = getChild<LLIconCtrl>("damage_icon");
    mParcelIcon[SEE_AVATARS_ICON] = getChild<LLIconCtrl>("see_avatars_icon");

    mParcelIcon[VOICE_ICON]->setToolTip(LLTrans::getString("LocationCtrlVoiceTooltip"));
    mParcelIcon[FLY_ICON]->setToolTip(LLTrans::getString("LocationCtrlFlyTooltip"));
    mParcelIcon[PUSH_ICON]->setToolTip(LLTrans::getString("LocationCtrlPushTooltip"));
    mParcelIcon[BUILD_ICON]->setToolTip(LLTrans::getString("LocationCtrlBuildTooltip"));
    mParcelIcon[SCRIPTS_ICON]->setToolTip(LLTrans::getString("LocationCtrlScriptsTooltip"));
    mParcelIcon[DAMAGE_ICON]->setToolTip(LLTrans::getString("LocationCtrlDamageTooltip"));
    mParcelIcon[SEE_AVATARS_ICON]->setToolTip(LLTrans::getString("LocationCtrlSeeAVsTooltip"));

    mParcelIcon[VOICE_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, VOICE_ICON));
    mParcelIcon[FLY_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, FLY_ICON));
    mParcelIcon[PUSH_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, PUSH_ICON));
    mParcelIcon[BUILD_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, BUILD_ICON));
    mParcelIcon[SCRIPTS_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, SCRIPTS_ICON));
    mParcelIcon[DAMAGE_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, DAMAGE_ICON));
    mParcelIcon[SEE_AVATARS_ICON]->setMouseDownCallback(boost::bind(&LLPanelTopInfoBar::onParcelIconClick, this, SEE_AVATARS_ICON));

    mDamageText->setText(LLStringExplicit("100%"));
}

void LLPanelTopInfoBar::handleLoginComplete()
{
    // An agent parcel update hasn't occurred yet, so
    // we have to manually set location and the icons.
    update();
}

bool LLPanelTopInfoBar::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    if(!LLUICtrl::CommitCallbackRegistry::getValue("TopInfoBar.Action"))
    {
        LLUICtrl::CommitCallbackRegistry::currentRegistrar()
                .add("TopInfoBar.Action", boost::bind(&LLPanelTopInfoBar::onContextMenuItemClicked, this, _2));
    }
    show_topinfobar_context_menu(this, x, y);
    return true;
}

bool LLPanelTopInfoBar::postBuild()
{
    mInfoBtn = getChild<LLButton>("place_info_btn");
    mInfoBtn->setClickedCallback(boost::bind(&LLPanelTopInfoBar::onInfoButtonClicked, this));
    mInfoBtn->setToolTip(LLTrans::getString("LocationCtrlInfoBtnTooltip"));

    mParcelInfoText = getChild<LLTextBox>("parcel_info_text");
    mParcelInfoText->setClickedCallback(boost::bind(&LLPanelTopInfoBar::onParcelInfoTextClicked, this));
    mDamageText = getChild<LLTextBox>("damage_text");

    initParcelIcons();

    mParcelChangedObserver = new LLParcelChangeObserver(this);
    LLViewerParcelMgr::getInstance()->addObserver(mParcelChangedObserver);

    // Connecting signal for updating parcel icons on "Show Parcel Properties" setting change.
    LLControlVariable* ctrl = gSavedSettings.getControl("NavBarShowParcelProperties").get();
    if (ctrl)
    {
        mParcelPropsCtrlConnection = ctrl->getSignal()->connect(boost::bind(&LLPanelTopInfoBar::updateParcelIcons, this));
    }

    // Connecting signal for updating parcel text on "Show Coordinates" setting change.
    ctrl = gSavedSettings.getControl("NavBarShowCoordinates").get();
    if (ctrl)
    {
        mShowCoordsCtrlConnection = ctrl->getSignal()->connect(boost::bind(&LLPanelTopInfoBar::onNavBarShowParcelPropertiesCtrlChanged, this));
    }

    mParcelMgrConnection = gAgent.addParcelChangedCallback(
            boost::bind(&LLPanelTopInfoBar::onAgentParcelChange, this));

    // An estate manager changing the region's maturity rating, or renaming it,
    // arrives on a handshake and moves neither the parcel nor the region we
    // are standing in -- so no other callback here sees it.
    mRegionInfoConnection = LLViewerRegion::setRegionInfoChangedCallback(
        boost::bind(&LLPanelTopInfoBar::onRegionInfoChanged, this, _1));

    mHealthConnection = gAgent.addHealthChangedCallback(
        boost::bind(&LLPanelTopInfoBar::setHealth, this, _1));

    setVisibleCallback(boost::bind(&LLPanelTopInfoBar::onVisibilityChanged, this, _2));

    return true;
}

void LLPanelTopInfoBar::onNavBarShowParcelPropertiesCtrlChanged()
{
    refreshParcelInfoText();
}

// when panel is shown, all minimized floaters should be shifted downwards to prevent overlapping of
// PanelTopInfoBar. See EXT-7951.
void LLPanelTopInfoBar::onVisibilityChanged(const LLSD& show)
{
    // this height is used as a vertical offset for ALREADY MINIMIZED floaters
    // when PanelTopInfoBar visibility changes
    S32 height = getRect().getHeight();

    // this vertical offset is used for a start minimize position of floaters that
    // are NOT MIMIMIZED YET
    S32 minimize_pos_offset = 0;

    if (show.asBoolean())
    {
        height = minimize_pos_offset = -height;
    }

    gFloaterView->shiftFloaters(0, height);
    gFloaterView->setMinimizePositionVerticalOffset(minimize_pos_offset);
}

boost::signals2::connection LLPanelTopInfoBar::setResizeCallback( const resize_signal_t::slot_type& cb )
{
    return mResizeSignal.connect(cb);
}

void LLPanelTopInfoBar::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    updateParcelInfoText();

    LLPanel::draw();
}

void LLPanelTopInfoBar::refreshParcelInfoText()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    // Recorded whether or not the text turns out to have moved: draw() reads
    // this to decide whether to come back here, and a caller that rebuilt
    // without recording would put the readout straight back onto the
    // per-frame path.
    LLAgentUI::getDisplayPos(mDisplayPosX, mDisplayPosY, mDisplayPosZ);

    static LLUICachedControl<bool> show_coords("NavBarShowCoordinates", false);
    LLAgentUI::ELocationFormat format =
        (show_coords ? LLAgentUI::LOCATION_FORMAT_FULL : LLAgentUI::LOCATION_FORMAT_NO_COORDS);

    if (!LLAgentUI::buildLocationString(mLocationScratch, format))
    {
        // Between a region crossing and the parcel properties that follow it
        // there is no parcel to name. Keep the last good string rather than
        // flashing a placeholder at every crossing: a refresh follows when the
        // parcel arrives, and the placeholder is only right before the first
        // one ever does.
        if (!mParcelInfoText->getText().empty())
        {
            return;
        }
        mLocationScratch = "???";
    }

    // The mean of any span of this plot is the fraction of rebuilds that were
    // worth making -- the number the gate in draw() is sized against. The text
    // box is asked rather than a copy of it being kept: it hands back a
    // reference to what it is already holding.
    LL_PROFILE_PLOT("topinfo location changed",
                    (int64_t)(mLocationScratch != mParcelInfoText->getText()));

    // Second gate, because the rounding buckets are 2 m at a walk and 4 m in
    // flight: the integers draw() compared can move without the text moving.
    if (mLocationScratch == mParcelInfoText->getText())
    {
        return;
    }

    setParcelInfoText(mLocationScratch);
}

void LLPanelTopInfoBar::setParcelInfoText(const std::string& new_text)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    const LLFontGL* font = mParcelInfoText->getFont();
    S32 new_text_width = font->getWidth(new_text);

    mParcelInfoText->setText(new_text);

    LLRect rect = mParcelInfoText->getRect();
    rect.setOriginAndSize(rect.mLeft, rect.mBottom, new_text_width, rect.getHeight());

    mParcelInfoText->reshape(rect.getWidth(), rect.getHeight(), true);
    mParcelInfoText->setRect(rect);

    // Nothing above this line touches the panel's own rect -- only the text
    // box's, which is a child. layoutParcelIcons is where the panel is
    // resized, and it is the one place that announces it. Comparing the rect
    // here as well meant one text change fired the resize signal twice, and
    // the listener on it ends in a full reshape of the chiclet bar.
    layoutParcelIcons();
}

void LLPanelTopInfoBar::update()
{
    refreshParcelInfoText();

    updateParcelIcons();

    // Health has a signal now, but nothing replays the last value to a panel
    // that was not listening when it arrived. This is the sync for that.
    setHealth(gAgent.getHealth());
}

void LLPanelTopInfoBar::updateParcelInfoText()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    static LLUICachedControl<bool> show_coords("NavBarShowCoordinates", false);
    if (!show_coords)
    {
        // Without coordinates in it the readout has no time-varying input at
        // all: every other thing it names arrives on a callback.
        return;
    }

    // The one input to this readout with no callback behind it is the agent's
    // own position, and the readout prints it rounded -- to 2 m at a walk, 4 m
    // in flight. Three integers answer whether the text can have moved.
    // Building the text to find out costs a format, five allocations, a
    // shaping pass and a segment rebuild of the text box, and for all but a
    // couple of frames a second the answer is no.
    S32 pos_x, pos_y, pos_z;
    LLAgentUI::getDisplayPos(pos_x, pos_y, pos_z);
    if (pos_x == mDisplayPosX && pos_y == mDisplayPosY && pos_z == mDisplayPosZ)
    {
        return;
    }

    refreshParcelInfoText();
}

void LLPanelTopInfoBar::updateParcelIcons()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    LLViewerParcelMgr* vpm = LLViewerParcelMgr::getInstance();

    LLViewerRegion* agent_region = gAgent.getRegion();
    LLParcel* agent_parcel = vpm->getAgentParcel();
    if (!agent_region || !agent_parcel)
        return;

    static LLUICachedControl<bool> show_icons("NavBarShowParcelProperties", false);
    if (show_icons)
    {
        LLParcel* current_parcel;
        LLViewerRegion* selection_region = vpm->getSelectionRegion();
        LLParcel* selected_parcel = vpm->getParcelSelection()->getParcel();

        // If agent is in selected parcel we use its properties because
        // they are updated more often by LLViewerParcelMgr than agent parcel properties.
        // See LLViewerParcelMgr::processParcelProperties().
        // This is needed to reflect parcel restrictions changes without having to leave
        // the parcel and then enter it again. See EXT-2987
        if (selected_parcel && selected_parcel->getLocalID() == agent_parcel->getLocalID()
                && selection_region == agent_region)
        {
            current_parcel = selected_parcel;
        }
        else
        {
            current_parcel = agent_parcel;
        }

        bool allow_voice    = vpm->allowAgentVoice(agent_region, current_parcel);
        bool allow_fly      = vpm->allowAgentFly(agent_region, current_parcel);
        bool allow_push     = vpm->allowAgentPush(agent_region, current_parcel);
        bool allow_build    = vpm->allowAgentBuild(current_parcel); // true when anyone is allowed to build. See EXT-4610.
        bool allow_scripts  = vpm->allowAgentScripts(agent_region, current_parcel);
        bool allow_damage   = vpm->allowAgentDamage(agent_region, current_parcel);
        bool see_avs        = current_parcel->getSeeAVs();

        // Most icons are "block this ability"
        mParcelIcon[VOICE_ICON]->setVisible(   !allow_voice );
        mParcelIcon[FLY_ICON]->setVisible(     !allow_fly );
        mParcelIcon[PUSH_ICON]->setVisible(    !allow_push );
        mParcelIcon[BUILD_ICON]->setVisible(   !allow_build );
        mParcelIcon[SCRIPTS_ICON]->setVisible( !allow_scripts );
        mParcelIcon[DAMAGE_ICON]->setVisible(  allow_damage );
        mDamageText->setVisible(allow_damage);
        mParcelIcon[SEE_AVATARS_ICON]->setVisible( !see_avs );

        layoutParcelIcons();
    }
    else
    {
        for (S32 i = 0; i < ICON_COUNT; ++i)
        {
            mParcelIcon[i]->setVisible(false);
        }
        mDamageText->setVisible(false);
    }
}

void LLPanelTopInfoBar::setHealth(S32 health)
{
    if (health == mLastHealth)
    {
        return;
    }
    mLastHealth = health;
    mDamageText->setText(fmt::format("{}%", health));
}

void LLPanelTopInfoBar::onRegionInfoChanged(LLViewerRegion* regionp)
{
    // Fires for every region that hands us a handshake, including neighbours
    // coming into view. Only the one being displayed matters.
    if (regionp != gAgent.getRegion())
    {
        return;
    }

    refreshParcelInfoText();
}

void LLPanelTopInfoBar::layoutParcelIcons()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    LLRect old_rect = getRect();

    // TODO: remove hard-coded values and read them as xml parameters
    static const int FIRST_ICON_HPAD = 32;
    static const int LAST_ICON_HPAD = 11;

    S32 left = mParcelInfoText->getRect().mRight + FIRST_ICON_HPAD;

    left = layoutWidget(mDamageText, left);

    for (int i = ICON_COUNT - 1; i >= 0; --i)
    {
        left = layoutWidget(mParcelIcon[i], left);
    }

    LLRect rect = getRect();
    rect.set(rect.mLeft, rect.mTop, left + LAST_ICON_HPAD, rect.mBottom);
    setRect(rect);

    if (old_rect != getRect())
    {
        mResizeSignal();
    }
}

S32 LLPanelTopInfoBar::layoutWidget(LLUICtrl* ctrl, S32 left)
{
    // TODO: remove hard-coded values and read them as xml parameters
    static const int ICON_HPAD = 2;

    if (ctrl->getVisible())
    {
        LLRect rect = ctrl->getRect();
        rect.mRight = left + rect.getWidth();
        rect.mLeft = left;

        ctrl->setRect(rect);
        left += rect.getWidth() + ICON_HPAD;
    }

    return left;
}

void LLPanelTopInfoBar::onParcelIconClick(EParcelIcon icon)
{
    switch (icon)
    {
    case VOICE_ICON:
        LLNotificationsUtil::add("NoVoice");
        break;
    case FLY_ICON:
        LLNotificationsUtil::add("NoFly");
        break;
    case PUSH_ICON:
        LLNotificationsUtil::add("PushRestricted");
        break;
    case BUILD_ICON:
        LLNotificationsUtil::add("NoBuild");
        break;
    case SCRIPTS_ICON:
    {
        LLViewerRegion* region = gAgent.getRegion();
        if(region && region->getRegionFlag(REGION_FLAGS_ESTATE_SKIP_SCRIPTS))
        {
            LLNotificationsUtil::add("ScriptsStopped");
        }
        else if(region && region->getRegionFlag(REGION_FLAGS_SKIP_SCRIPTS))
        {
            LLNotificationsUtil::add("ScriptsNotRunning");
        }
        else
        {
            LLNotificationsUtil::add("NoOutsideScripts");
        }
        break;
    }
    case DAMAGE_ICON:
        LLNotificationsUtil::add("NotSafe");
        break;
    case SEE_AVATARS_ICON:
        LLNotificationsUtil::add("SeeAvatars");
        break;
    case ICON_COUNT:
        break;
    // no default to get compiler warning when a new icon gets added
    }
}

void LLPanelTopInfoBar::onAgentParcelChange()
{
    update();
}

void LLPanelTopInfoBar::onContextMenuItemClicked(const LLSD::String& item)
{
    if (item == "landmark")
    {
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
        if (!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        {
// [/RLVa:KB]
            LLViewerInventoryItem* landmark = LLLandmarkActions::findLandmarkForAgentPos();

            if(landmark == NULL)
            {
                LLFloaterReg::showInstance("add_landmark");
            }
            else
            {
                LLFloaterSidePanelContainer::showPanel("places", LLSD().with("type", "landmark").with("id",landmark->getUUID()));
            }
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
        }
// [/RLVa:KB]
    }
    else if (item == "copy")
    {
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
        if (!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        {
// [/RLVa:KB]
            LLSLURL slurl;
            LLAgentUI::buildSLURL(slurl, false);
            LLUIString location_str(slurl.getSLURLString());

            LLClipboard::instance().copyToClipboard(location_str.getString(), 0, location_str.lengthBytes());
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
        }
// [/RLVa:KB]
    }
}

void LLPanelTopInfoBar::onInfoButtonClicked()
{
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        return;
// [/RLVa:KB]

    LLFloaterSidePanelContainer::showPanel("places", LLSD().with("type", "agent"));
}

void LLPanelTopInfoBar::onParcelInfoTextClicked()
{
// [RLVa:KB] - Checked: 2012-02-08 (RLVa-1.4.5) | Added: RLVa-1.4.5
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        return;
// [/RLVa:KB]

    LLFloaterReg::showInstance("about_land");
}
