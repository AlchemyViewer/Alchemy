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
#include "lliconctrl.h"
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
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_VOICE,       getChild<LLIconCtrl>("voice_icon"));
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_FLY,         getChild<LLIconCtrl>("fly_icon"));
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_PUSH,        getChild<LLIconCtrl>("push_icon"));
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_BUILD,       getChild<LLIconCtrl>("build_icon"));
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_SCRIPTS,     getChild<LLIconCtrl>("scripts_icon"));
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_DAMAGE,      getChild<LLIconCtrl>("damage_icon"));
    mParcelIcons.setIcon(ALParcelIconStrip::ICON_SEE_AVATARS, getChild<LLIconCtrl>("see_avatars_icon"));
    // The pathfinding pair belongs to the navigation bar; this panel has no
    // controls for them and leaves those two slots empty.
    mParcelIcons.setDamageText(getChild<LLTextBox>("damage_text"));
    mParcelIcons.initIcons();
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
        [this](S32 health) { mParcelIcons.setHealth(health); });

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

    S32 pos_x, pos_y, pos_z;
    LLAgentUI::getDisplayPos(pos_x, pos_y, pos_z);

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
        //
        // The position is deliberately not recorded here. What draw() compares
        // it against is the position the text on screen was built from, and
        // the text on screen is now older than this one -- recording it would
        // tell draw() the readout was current and stop it coming back.
        if (!mParcelInfoText->getText().empty())
        {
            return;
        }
        mLocationScratch = "???";
    }

    // Recorded whether or not the text turns out to have moved, because from
    // here on the string is the one this position produces: draw() reads this
    // to decide whether to come back, and not recording it would put the
    // readout straight back onto the per-frame path.
    mDisplayPosX = pos_x;
    mDisplayPosY = pos_y;
    mDisplayPosZ = pos_z;

    // The text box is asked rather than a copy of it being kept: it hands back
    // a reference to what it is already holding.
    //
    // Second gate, because the rounding buckets are 2 m at a walk and 4 m in
    // flight: the integers draw() compared can move without the text moving.
    const bool text_moved = mLocationScratch != mParcelInfoText->getText();

    // The mean of any span of this plot is the fraction of rebuilds that were
    // worth making -- the number the gate in draw() is sized against.
    LL_PROFILE_PLOT("topinfo location changed", (int64_t)text_moved);

    if (!text_moved)
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
    mParcelIcons.setHealth(gAgent.getHealth());
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

    static LLUICachedControl<bool> show_icons("NavBarShowParcelProperties", false);
    mParcelIcons.update(show_icons);
    layoutParcelIcons();
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
    static const int ICON_HPAD = 2;

    const S32 right = mParcelIcons.layout(mParcelInfoText->getRect().mRight + FIRST_ICON_HPAD,
                                          ALParcelIconStrip::LAYOUT_RIGHTWARD,
                                          ICON_HPAD);

    LLRect rect = getRect();
    rect.set(rect.mLeft, rect.mTop, right + LAST_ICON_HPAD, rect.mBottom);
    setRect(rect);

    if (old_rect != getRect())
    {
        mResizeSignal();
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
