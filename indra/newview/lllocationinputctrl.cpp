/**
 * @file lllocationinputctrl.cpp
 * @brief Combobox-like location input control
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

#include <fmt/format.h>

// file includes
#include "lllocationinputctrl.h"

// common includes
#include "llbutton.h"
#include "llfocusmgr.h"
#include "llhelp.h"
#include "llmenugl.h"
#include "llparcel.h"
#include "llstring.h"
#include "lltrans.h"
#include "lluictrlfactory.h"
#include "lltooltip.h"
#include "llnotificationsutil.h"
#include "llregionflags.h"

// newview includes
#include "llagent.h"
#include "llfloaterreg.h"
#include "llfloatersidepanelcontainer.h"
#include "llinventoryobserver.h"
#include "lllandmarkactions.h"
#include "lllandmarklist.h"
#include "llpathfindingmanager.h"
#include "llpathfindingnavmesh.h"
#include "llpathfindingnavmeshstatus.h"
#include "llteleporthistory.h"
#include "llslurl.h"
#include "lltrans.h"
#include "llviewerinventory.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"
#include "llurllineeditorctrl.h"
#include "llagentui.h"
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.2.0d)
#include "rlvhandler.h"
// [/RLVa:KB]

#include "llmenuoptionpathfindingrebakenavmesh.h"
#include "llpathfindingmanager.h"

//============================================================================
/*
 * "ADD LANDMARK" BUTTON UPDATING LOGIC
 *
 * If the current parcel has been landmarked, we should draw
 * a special image on the button.
 *
 * To avoid determining the appropriate image on every draw() we do that
 * only in the following cases:
 * 1) Navbar is shown for the first time after login.
 * 2) Agent moves to another parcel.
 * 3) A landmark is created or removed.
 *
 * The first case is handled by the handleLoginComplete() method.
 *
 * The second case is handled by setting the "agent parcel changed" callback
 * on LLViewerParcelMgr.
 *
 * The third case is the most complex one. We have two inventory observers for that:
 * one is designated to handle adding landmarks, the other handles removal.
 * Let's see how the former works.
 *
 * When we get notified about landmark addition, the landmark position is unknown yet. What we can
 * do at that point is initiate loading the landmark data by LLLandmarkList and set the
 * "loading finished" callback on it. Finally, when the callback is triggered,
 * we can determine whether the landmark refers to a point within the current parcel
 * and choose the appropriate image for the "Add landmark" button.
 */

/**
 * Initiates loading the landmarks that have been just added.
 *
 * Once the loading is complete we'll be notified
 * with the callback we set for LLLandmarkList.
 */
class LLAddLandmarkObserver : public LLInventoryAddedObserver
{
public:
    LLAddLandmarkObserver(LLLocationInputCtrl* input) : mInput(input) {}

private:
    /*virtual*/ void done()
    {
        const uuid_set_t& added = gInventory.getAddedIDs();
        for (uuid_set_t::const_iterator it = added.begin(); it != added.end(); ++it)
        {
            LLInventoryItem* item = gInventory.getItem(*it);
            if (!item || item->getType() != LLAssetType::AT_LANDMARK)
                continue;

            // Start loading the landmark.
            LLLandmark* lm = gLandmarkList.getAsset(
                    item->getAssetUUID(),
                    boost::bind(&LLLocationInputCtrl::onLandmarkLoaded, mInput, _1));
            if (lm)
            {
                // Already loaded? Great, handle it immediately (the callback won't be called).
                mInput->onLandmarkLoaded(lm);
            }
        }
    }

    LLLocationInputCtrl* mInput;
};

/**
 * Updates the "Add landmark" button once a landmark gets removed.
 */
class LLRemoveLandmarkObserver : public LLInventoryObserver
{
public:
    LLRemoveLandmarkObserver(LLLocationInputCtrl* input) : mInput(input) {}

private:
    /*virtual*/ void changed(U32 mask)
    {
        if (mask & (~(LLInventoryObserver::LABEL|
                      LLInventoryObserver::INTERNAL|
                      LLInventoryObserver::ADD|
                      LLInventoryObserver::CREATE|
                      LLInventoryObserver::UPDATE_CREATE)))
        {
            mInput->updateAddLandmarkButton();
        }
    }

    LLLocationInputCtrl* mInput;
};

class LLParcelChangeObserver : public LLParcelObserver
{
public:
    LLParcelChangeObserver(LLLocationInputCtrl* input) : mInput(input) {}

private:
    /*virtual*/ void changed()
    {
        if (mInput)
        {
            // The text as well as the icons. The parcel's name is half of what
            // the location field says, and a rename used to reach the field
            // only because the field was rebuilt on every frame anyway.
            //
            // Not refresh(): this fires for any parcel whose properties
            // arrive, including one merely selected in About Land, and the
            // landmark button that refresh() also updates depends on the
            // parcel the agent is standing in. That has its own callback.
            mInput->refreshLocation();
            mInput->refreshParcelIcons();
        }
    }

    LLLocationInputCtrl* mInput;
};

//============================================================================


static LLDefaultChildRegistry::Register<LLLocationInputCtrl> r("location_input");

LLLocationInputCtrl::Params::Params()
:   icon_maturity_general("icon_maturity_general"),
    icon_maturity_adult("icon_maturity_adult"),
    icon_maturity_moderate("icon_maturity_moderate"),
    add_landmark_image_enabled("add_landmark_image_enabled"),
    add_landmark_image_disabled("add_landmark_image_disabled"),
    add_landmark_image_hover("add_landmark_image_hover"),
    add_landmark_image_selected("add_landmark_image_selected"),
    add_landmark_hpad("add_landmark_hpad", 0),
    icon_hpad("icon_hpad", 0),
    add_landmark_button("add_landmark_button"),
    for_sale_button("for_sale_button"),
    info_button("info_button"),
    maturity_button("maturity_button"),
    voice_icon("voice_icon"),
    fly_icon("fly_icon"),
    push_icon("push_icon"),
    build_icon("build_icon"),
    scripts_icon("scripts_icon"),
    damage_icon("damage_icon"),
    damage_text("damage_text"),
    see_avatars_icon("see_avatars_icon"),
    maturity_help_topic("maturity_help_topic"),
    pathfinding_dirty_icon("pathfinding_dirty_icon"),
    pathfinding_disabled_icon("pathfinding_disabled_icon")
{
}

LLLocationInputCtrl::LLLocationInputCtrl(const LLLocationInputCtrl::Params& p)
:   LLComboBox(p),
    mIconHPad(p.icon_hpad),
    mAddLandmarkHPad(p.add_landmark_hpad),
    mLocationContextMenu(NULL),
    mAddLandmarkBtn(NULL),
    mForSaleBtn(NULL),
    mInfoBtn(NULL),
    mRegionCrossingSlot(),
    mNavMeshSlot(),
    mLandmarkImageOn(NULL),
    mLandmarkImageOff(NULL),
    mIconMaturityGeneral(NULL),
    mIconMaturityAdult(NULL),
    mIconMaturityModerate(NULL),
    mMaturityHelpTopic(p.maturity_help_topic)
{
    // Lets replace default LLLineEditor with LLLocationLineEditor
    // to make needed escaping while copying and cutting url
    delete mTextEntry;

    // Can't access old mTextEntry fields as they are protected, so lets build new params
    // That is C&P from LLComboBox::createLineEditor function
    S32 arrow_width = mArrowImage ? mArrowImage->getWidth() : 0;
    LLRect text_entry_rect(0, getRect().getHeight(), getRect().getWidth(), 0);
    text_entry_rect.mRight -= llmax(8,arrow_width) + 2 * BTN_DROP_SHADOW;

    LLLineEditor::Params params = p.combo_editor;
    params.rect(text_entry_rect);
    params.default_text(LLStringUtil::null);
    params.max_length.bytes(p.max_chars);
    params.keystroke_callback(boost::bind(&LLLocationInputCtrl::onTextEntry, this, _1));
    params.commit_on_focus_lost(false);
    params.follows.flags(FOLLOWS_ALL);
    mTextEntry = LLUICtrlFactory::create<LLURLLineEditor>(params);
    mTextEntry->resetContextMenu();
    addChild(mTextEntry);
    // LLLineEditor is replaced with LLLocationLineEditor

    // "Place information" button.
    LLButton::Params info_params = p.info_button;
    mInfoBtn = LLUICtrlFactory::create<LLButton>(info_params);
    mInfoBtn->setClickedCallback(boost::bind(&LLLocationInputCtrl::onInfoButtonClicked, this));
    addChild(mInfoBtn);

    // "Add landmark" button.
    LLButton::Params al_params = p.add_landmark_button;

    // Image for unselected state will be set in updateAddLandmarkButton(),
    // it will be either mLandmarkOn or mLandmarkOff
    if (p.add_landmark_image_enabled())
    {
        mLandmarkImageOn = p.add_landmark_image_enabled;
    }
    if (p.add_landmark_image_disabled())
    {
        mLandmarkImageOff = p.add_landmark_image_disabled;
    }

    if(p.add_landmark_image_selected)
    {
        al_params.image_selected = p.add_landmark_image_selected;
    }
    if (p.add_landmark_image_hover())
    {
        al_params.image_hover_unselected = p.add_landmark_image_hover;
    }

    al_params.click_callback.function(boost::bind(&LLLocationInputCtrl::onAddLandmarkButtonClicked, this));
    mAddLandmarkBtn = LLUICtrlFactory::create<LLButton>(al_params);
    enableAddLandmarkButton(true);
    addChild(mAddLandmarkBtn);

    if (p.icon_maturity_general())
    {
        mIconMaturityGeneral = p.icon_maturity_general;
    }
    if (p.icon_maturity_adult())
    {
        mIconMaturityAdult = p.icon_maturity_adult;
    }
    if(p.icon_maturity_moderate())
    {
        mIconMaturityModerate = p.icon_maturity_moderate;
    }

    LLButton::Params maturity_button = p.maturity_button;
    mMaturityButton = LLUICtrlFactory::create<LLButton>(maturity_button);
    addChild(mMaturityButton);

    LLButton::Params for_sale_button = p.for_sale_button;
    for_sale_button.tool_tip = LLTrans::getString("LocationCtrlForSaleTooltip");
    for_sale_button.click_callback.function(
        boost::bind(&LLLocationInputCtrl::onForSaleButtonClicked, this));
    mForSaleBtn = LLUICtrlFactory::create<LLButton>( for_sale_button );
    addChild(mForSaleBtn);

    // Parcel property icons
    // Must be mouse-opaque so cursor stays as an arrow when hovering to
    // see tooltip. Tooltips and the click handlers that are the same in both
    // panels come from the strip's initIcons below; the order these are added
    // in is the order they were added in before.
    auto make_icon = [this](const LLIconCtrl::Params& base, ALParcelIconStrip::EIcon slot)
    {
        LLIconCtrl::Params params = base;
        params.mouse_opaque = true;
        LLIconCtrl* ctrl = LLUICtrlFactory::create<LLIconCtrl>(params);
        mParcelIcons.setIcon(slot, ctrl);
        addChild(ctrl);
        return ctrl;
    };

    make_icon(p.voice_icon,   ALParcelIconStrip::ICON_VOICE);
    make_icon(p.fly_icon,     ALParcelIconStrip::ICON_FLY);
    make_icon(p.push_icon,    ALParcelIconStrip::ICON_PUSH);
    make_icon(p.build_icon,   ALParcelIconStrip::ICON_BUILD);
    make_icon(p.scripts_icon, ALParcelIconStrip::ICON_SCRIPTS);
    make_icon(p.damage_icon,  ALParcelIconStrip::ICON_DAMAGE);

    make_icon(p.pathfinding_dirty_icon, ALParcelIconStrip::ICON_PATHFINDING_DIRTY)
        ->setMouseDownCallback(boost::bind(&LLLocationInputCtrl::onPathfindingIconClick, this,
                                           ALParcelIconStrip::ICON_PATHFINDING_DIRTY));
    make_icon(p.pathfinding_disabled_icon, ALParcelIconStrip::ICON_PATHFINDING_DISABLED)
        ->setMouseDownCallback(boost::bind(&LLLocationInputCtrl::onPathfindingIconClick, this,
                                           ALParcelIconStrip::ICON_PATHFINDING_DISABLED));

    LLTextBox::Params damage_text = p.damage_text;
    damage_text.tool_tip = LLTrans::getString("LocationCtrlDamageTooltip");
    damage_text.mouse_opaque = true;
    LLTextBox* damage_ctrl = LLUICtrlFactory::create<LLTextBox>(damage_text);
    addChild(damage_ctrl);
    mParcelIcons.setDamageText(damage_ctrl);

    make_icon(p.see_avatars_icon, ALParcelIconStrip::ICON_SEE_AVATARS);

    mParcelIcons.initIcons();

    // Register callbacks and load the location field context menu (NB: the order matters).
    LLUICtrl::CommitCallbackRegistry::currentRegistrar().add("Navbar.Action", boost::bind(&LLLocationInputCtrl::onLocationContextMenuItemClicked, this, _2));
    LLUICtrl::EnableCallbackRegistry::currentRegistrar().add("Navbar.EnableMenuItem", boost::bind(&LLLocationInputCtrl::onLocationContextMenuItemEnabled, this, _2));

    setPrearrangeCallback(boost::bind(&LLLocationInputCtrl::onLocationPrearrange, this, _2));
    getTextEntry()->setMouseUpCallback(boost::bind(&LLLocationInputCtrl::changeLocationPresentation, this));

    // Load the location field context menu
    mLocationContextMenu = LLUICtrlFactory::getInstance()->createFromFile<LLMenuGL>("menu_navbar.xml", gMenuHolder, LLViewerMenuHolderGL::child_registry_t::instance());
    if (!mLocationContextMenu)
    {
        LL_WARNS() << "Error loading navigation bar context menu" << LL_ENDL;

    }
    //don't show default context menu
    getTextEntry()->setShowContextMenu(false);
    getTextEntry()->setRightMouseDownCallback(boost::bind(&LLLocationInputCtrl::onTextEditorRightClicked, this, _2, _3, _4));
    updateWidgetlayout();

    // Connecting signal for updating location on "Show Coordinates" setting change.
    LLControlVariable* coordinates_control = gSavedSettings.getControl("NavBarShowCoordinates").get();
    if (coordinates_control)
    {
        mCoordinatesControlConnection = coordinates_control->getSignal()->connect(boost::bind(&LLLocationInputCtrl::refreshLocation, this));
    }

    // Connecting signal for updating parcel icons on "Show Parcel Properties" setting change.
    LLControlVariable* parcel_properties_control = gSavedSettings.getControl("NavBarShowParcelProperties").get();
    if (parcel_properties_control)
    {
        mParcelPropertiesControlConnection = parcel_properties_control->getSignal()->connect(boost::bind(&LLLocationInputCtrl::refreshParcelIcons, this));
    }

    // - Make the "Add landmark" button updated when either current parcel gets changed
    //   or a landmark gets created or removed from the inventory.
    // - Update the location string on parcel change.
    mParcelMgrConnection = gAgent.addParcelChangedCallback(
        boost::bind(&LLLocationInputCtrl::onAgentParcelChange, this));
    // LLLocationHistory instance is being created before the location input control, so we have to update initial state of button manually.
    mButton->setEnabled(LLLocationHistory::instance().getItemCount() > 0);
    mLocationHistoryConnection = LLLocationHistory::getInstance()->setChangedCallback(
            boost::bind(&LLLocationInputCtrl::onLocationHistoryChanged, this,_1));

    mRegionCrossingSlot = gAgent.addRegionChangedCallback(boost::bind(&LLLocationInputCtrl::onRegionBoundaryCrossed, this));
    // An estate manager changing the region's maturity rating, or renaming it,
    // arrives on a handshake and moves neither the parcel nor the region we
    // are standing in -- so no other callback here sees it.
    mRegionInfoConnection = LLViewerRegion::setRegionInfoChangedCallback(
        boost::bind(&LLLocationInputCtrl::onRegionInfoChanged, this, _1));
    mHealthConnection = gAgent.addHealthChangedCallback(
        [this](S32 health) { mParcelIcons.setHealth(health); });
    createNavMeshStatusListenerForCurrentRegion();

    mRemoveLandmarkObserver = new LLRemoveLandmarkObserver(this);
    mAddLandmarkObserver    = new LLAddLandmarkObserver(this);
    gInventory.addObserver(mRemoveLandmarkObserver);
    gInventory.addObserver(mAddLandmarkObserver);

    mParcelChangeObserver = new LLParcelChangeObserver(this);
    LLViewerParcelMgr::getInstance()->addObserver(mParcelChangeObserver);

    mAddLandmarkTooltip = LLTrans::getString("LocationCtrlAddLandmarkTooltip");
    mEditLandmarkTooltip = LLTrans::getString("LocationCtrlEditLandmarkTooltip");
    mButton->setToolTip(LLTrans::getString("LocationCtrlComboBtnTooltip"));
    mInfoBtn->setToolTip(LLTrans::getString("LocationCtrlInfoBtnTooltip"));
}

LLLocationInputCtrl::~LLLocationInputCtrl()
{
    gInventory.removeObserver(mRemoveLandmarkObserver);
    gInventory.removeObserver(mAddLandmarkObserver);
    delete mRemoveLandmarkObserver;
    delete mAddLandmarkObserver;

    LLViewerParcelMgr::getInstance()->removeObserver(mParcelChangeObserver);
    delete mParcelChangeObserver;

    mRegionCrossingSlot.disconnect();
    mRegionInfoConnection.disconnect();
    mHealthConnection.disconnect();
    mNavMeshSlot.disconnect();
    mCoordinatesControlConnection.disconnect();
    mParcelPropertiesControlConnection.disconnect();
    mParcelMgrConnection.disconnect();
    mLocationHistoryConnection.disconnect();
}

void LLLocationInputCtrl::setEnabled(bool enabled)
{
    LLComboBox::setEnabled(enabled);
    mAddLandmarkBtn->setEnabled(enabled);
}

void LLLocationInputCtrl::hideList()
{
    LLComboBox::hideList();
    if (mTextEntry && hasFocus())
        focusTextEntry();
}

bool LLLocationInputCtrl::handleToolTip(S32 x, S32 y, MASK mask)
{

    if(mAddLandmarkBtn->parentPointInView(x,y))
    {
        updateAddLandmarkTooltip();
    }
    // Let the buttons show their tooltips.
    if (LLUICtrl::handleToolTip(x, y, mask))
    {
        if (mList->getRect().pointInRect(x, y))
        {
            S32 loc_x, loc_y;
            //x,y - contain coordinates related to the location input control, but without taking the expanded list into account
            //So we have to convert it again into local coordinates of mList
            localPointToOtherView(x,y,&loc_x,&loc_y,mList);

            LLScrollListItem* item =  mList->hitItem(loc_x,loc_y);
            if (item)
            {
                LLSD value = item->getValue();
                if (value.has("tooltip"))
                {
                    LLToolTipMgr::instance().show(value["tooltip"]);
                }
            }
        }

        return true;
    }

    return false;
}

bool LLLocationInputCtrl::handleKeyHere(KEY key, MASK mask)
{
    bool result = LLComboBox::handleKeyHere(key, mask);

    if (key == KEY_DOWN && hasFocus() && mList->getItemCount() != 0 && !mList->getVisible())
    {
        showList();
    }

    return result;
}

void LLLocationInputCtrl::onTextEntry(LLLineEditor* line_editor)
{
    KEY key = gKeyboard->currentKey();
    MASK mask = gKeyboard->currentMask(true);

    // Typing? (moving cursor should not affect showing the list)
    bool typing = mask != MASK_CONTROL && key != KEY_LEFT && key != KEY_RIGHT && key != KEY_HOME && key != KEY_END;
    bool pasting = mask == MASK_CONTROL && key == 'V';

    if (line_editor->getText().empty())
    {
        prearrangeList(); // resets filter
        hideList();
    }
    else if (typing || pasting)
    {
        prearrangeList(line_editor->getText());
        if (mList->getItemCount() != 0)
        {
            showList();
            focusTextEntry();
        }
        else
        {
            // Hide the list if it's empty.
            hideList();
        }
    }

    LLComboBox::onTextEntry(line_editor);
}

/**
 * Useful if we want to just set the text entry value, no matter what the list contains.
 *
 * This is faster than setTextEntry().
 */
void LLLocationInputCtrl::setText(ALStringViewExplicit text)
{
    if (mTextEntry)
    {
        mTextEntry->setText(text);
    }
    mHasAutocompletedText = false;
}

void LLLocationInputCtrl::setFocus(bool b)
{
    LLComboBox::setFocus(b);

    if (mTextEntry && b && !mList->getVisible())
    {
        mTextEntry->setFocus(true);
    }
}

void LLLocationInputCtrl::handleLoginComplete()
{
    // An agent parcel update hasn't occurred yet, so we have to
    // manually set location and the appropriate "Add landmark" icon.
    refresh();
}

//== private methods =========================================================

void LLLocationInputCtrl::onFocusReceived()
{
    prearrangeList();
}

void LLLocationInputCtrl::onFocusLost()
{
    LLUICtrl::onFocusLost();
    refreshLocation();

    // Setting cursor to 0  to show the left edge of the text. See STORM-370.
    mTextEntry->setCursor(0);

    if(mTextEntry->hasSelection()){
        mTextEntry->deselect();
    }
}

void LLLocationInputCtrl::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    static LLUICachedControl<bool> show_coords("NavBarShowCoordinates", false);
    if(!hasFocus() && show_coords)
    {
        // The one input to this readout with no callback behind it is the
        // agent's own position, and the readout prints it rounded -- to 2 m at
        // a walk, 4 m in flight. Three integers answer whether the text can
        // have moved. Building the text to find out costs a format, five
        // allocations and a shaping pass, and for all but a couple of frames a
        // second the answer is no.
        S32 pos_x, pos_y, pos_z;
        LLAgentUI::getDisplayPos(pos_x, pos_y, pos_z);
        if (pos_x != mDisplayPosX || pos_y != mDisplayPosY || pos_z != mDisplayPosZ)
        {
            refreshLocation();
        }
    }

    LLComboBox::draw();
}

void LLLocationInputCtrl::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLComboBox::reshape(width, height, called_from_parent);

    // Setting cursor to 0  to show the left edge of the text. See EXT-4967.
    mTextEntry->setCursor(0);
    if (mTextEntry->hasSelection())
    {
        // Deselecting because selection position is changed together with
        // cursor position change.
        mTextEntry->deselect();
    }

    if (isHumanReadableLocationVisible)
    {
        // Only the placement: the region's rating has not changed, the space
        // the text leaves for its icon has.
        positionMaturityButton();
    }
}

void LLLocationInputCtrl::onInfoButtonClicked()
{
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        return;
// [/RLVa:KB]

    LLFloaterSidePanelContainer::showPanel("places", LLSD().with("type", "agent"));
}

void LLLocationInputCtrl::onForSaleButtonClicked()
{
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        return;
// [/RLVa:KB]

    handle_buy_land();
}

void LLLocationInputCtrl::onAddLandmarkButtonClicked()
{
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
    if (gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        return;
// [/RLVa:KB]

    LLViewerInventoryItem* landmark = LLLandmarkActions::findLandmarkForAgentPos();
    // Landmark exists, open it for preview and edit
    if(landmark && landmark->getUUID().notNull())
    {
        LLSD key;
        key["type"] = "landmark";
        key["id"] = landmark->getUUID();

        LLFloaterSidePanelContainer::showPanel("places", key);
    }
    else
    {
        LLFloaterReg::showInstance("add_landmark");
    }
}

void LLLocationInputCtrl::onAgentParcelChange()
{
    refresh();
}

void LLLocationInputCtrl::onRegionBoundaryCrossed()
{
    createNavMeshStatusListenerForCurrentRegion();
    // The region's name and its maturity rating are both in the readout, and
    // both just changed. This reached the field only through the per-frame
    // rebuild before -- crossing a region usually crosses a parcel too, and
    // that is what happened to refresh it.
    refresh();
}

void LLLocationInputCtrl::onRegionInfoChanged(LLViewerRegion* regionp)
{
    // Fires for every region that hands us a handshake, including neighbours
    // coming into view. Only the one being displayed matters.
    if (regionp == gAgent.getRegion())
    {
        updateMaturityButtonImage();
        refreshLocation();
    }
}

void LLLocationInputCtrl::onNavMeshStatusChange(const LLPathfindingNavMeshStatus &pNavMeshStatus)
{
    mParcelIcons.setNavMeshDirty(pNavMeshStatus.isValid() && (pNavMeshStatus.getStatus() != LLPathfindingNavMeshStatus::kComplete));
    refreshParcelIcons();
}

void LLLocationInputCtrl::onLandmarkLoaded(LLLandmark* lm)
{
    (void) lm;
    updateAddLandmarkButton();
}

void LLLocationInputCtrl::onLocationHistoryChanged(LLLocationHistory::EChangeType event)
{
    if(event == LLLocationHistory::LOAD)
    {
        rebuildLocationHistory();
    }
    mButton->setEnabled(LLLocationHistory::instance().getItemCount() > 0);
}

void LLLocationInputCtrl::onLocationPrearrange(const LLSD& data)
{
    std::string filter = data.asString();
    rebuildLocationHistory(filter);

    //Let's add landmarks to the top of the list if any
    if(!filter.empty() )
    {
        LLInventoryModel::item_array_t landmark_items = LLLandmarkActions::fetchLandmarksByName(filter, true);

        for(U32 i=0; i < landmark_items.size(); i++)
        {
            LLSD value;
            //TODO:: DO we need tooltip for Landmark??

            value["item_type"] = LANDMARK;
            value["AssetUUID"] =  landmark_items[i]->getAssetUUID();
            addLocationHistoryEntry(landmark_items[i]->getName(), value);

        }
    //Let's add teleport history items
        LLTeleportHistory* th = LLTeleportHistory::getInstance();
        LLTeleportHistory::slurl_list_t th_items = th->getItems();

        std::set<std::string> new_item_titles;// duplicate control
        LLTeleportHistory::slurl_list_t::iterator result = std::find_if(
                th_items.begin(), th_items.end(), boost::bind(
                        &LLLocationInputCtrl::findTeleportItemsByTitle, this,
                        _1, filter));

        while (result != th_items.end())
        {
            //mTitile format - region_name[, parcel_name]
            //mFullTitile format - region_name[, parcel_name] (local_x,local_y, local_z)
            if (new_item_titles.insert(result->mFullTitle).second)
            {
                LLSD value;
                value["item_type"] = TELEPORT_HISTORY;
                value["global_pos"] = result->mGlobalPos.getValue();
                std::string region_name = result->mTitle.substr(0, result->mTitle.find(','));
                //TODO*: add Surl to teleportitem or parse region name from title
                value["tooltip"] = LLSLURL(region_name, result->mGlobalPos).getSLURLString();
                addLocationHistoryEntry(result->getTitle(), value);
            }
            result = std::find_if(result + 1, th_items.end(), boost::bind(
                                    &LLLocationInputCtrl::findTeleportItemsByTitle, this,
                                    _1, filter));
        }
    }
    sortByName();

    mList->mouseOverHighlightNthItem(-1); // Clear highlight on the last selected item.
}

bool LLLocationInputCtrl::findTeleportItemsByTitle(const LLTeleportHistoryItem& item, const std::string& filter)
{
    return item.mTitle.find(filter) != std::string::npos;
}

void LLLocationInputCtrl::onTextEditorRightClicked(S32 x, S32 y, MASK mask)
{
    if (mLocationContextMenu)
    {
        updateContextMenu();
        mLocationContextMenu->buildDrawLabels();
        mLocationContextMenu->updateParent(LLMenuGL::sMenuContainer);
        hideList();
        setFocus(true);
        changeLocationPresentation();
        LLMenuGL::showPopup(this, mLocationContextMenu, x, y);
    }
}

void LLLocationInputCtrl::refresh()
{
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
    mInfoBtn->setEnabled(!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC));
// [/RLVa:KB]

    updateMaturityButtonImage();
    refreshLocation();          // update location string
    refreshParcelIcons();
    updateAddLandmarkButton();  // indicate whether current parcel has been landmarked

    // Health has a signal now, but nothing replays the last value to a panel
    // that was not listening when it arrived. This is the sync for that.
    mParcelIcons.setHealth(gAgent.getHealth());
}

void LLLocationInputCtrl::refreshLocation()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    // Is one of our children focused?
    if (LLUICtrl::hasFocus() || mButton->hasFocus() || mList->hasFocus() ||
        (mTextEntry && mTextEntry->hasFocus()) ||
        (mAddLandmarkBtn->hasFocus()))
    {
        // Not a fault. Refreshes arrive on parcel, region and setting changes
        // now, and any of them can land while the user is part-way through
        // typing an address -- which is exactly when the field must be left
        // alone. It gets rewritten on focus loss.
        LL_DEBUGS("Navbar") << "Location refresh skipped: the field has focus" << LL_ENDL;
        return;
    }

    S32 pos_x, pos_y, pos_z;
    LLAgentUI::getDisplayPos(pos_x, pos_y, pos_z);

    // Cached: this is reached from draw, on every frame the setting is on, and
    // looking a setting up by its name is a hash of the name and a walk of the
    // map. The draw asks the same question through LLUICachedControl to decide
    // whether to call this at all.
    static LLCachedControl<bool> show_coordinates(gSavedSettings, "NavBarShowCoordinates", false);
    LLAgentUI::ELocationFormat format =
        (show_coordinates
            ? LLAgentUI::LOCATION_FORMAT_FULL
            : LLAgentUI::LOCATION_FORMAT_NO_COORDS);

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
        if (!mHumanReadableLocation.empty())
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

    // Second gate, because the rounding buckets are 2 m at a walk and 4 m in
    // flight: the integers draw() compared can move without the text moving.
    //
    // The question is asked of the field and not of mHumanReadableLocation,
    // which is a record of what this function last built rather than of what
    // is on screen. Two things put something else there: the SLURL the user
    // clicks for, and whatever the user typed before giving up focus. Putting
    // the readable text back over both is this function's other job, and a
    // gate that compared its own last answer would skip doing it.
    const bool text_moved = !mTextEntry || mTextEntry->getText() != mLocationScratch;

    // The mean of any span of this plot is the fraction of rebuilds that
    // produced different text -- the number draw()'s gate is sized against.
    // The same predicate the gate uses, so the two cannot drift apart.
    LL_PROFILE_PLOT("navbar location changed", (int64_t)text_moved);

    if (!text_moved)
    {
        return;
    }

    // store human-readable location to compare it in changeLocationPresentation()
    mHumanReadableLocation = mLocationScratch;
    setText(mHumanReadableLocation);
    isHumanReadableLocationVisible = true;

    // Only the placement: the text is what changed, not the region's rating.
    positionMaturityButton();
}

// returns new right edge
static S32 layout_widget(LLUICtrl* widget, S32 right)
{
    if (widget->getVisible())
    {
        LLRect rect = widget->getRect();
        rect.mLeft = right - rect.getWidth();
        rect.mRight = right;
        widget->setRect( rect );
        right -= rect.getWidth();
    }
    return right;
}

void LLLocationInputCtrl::refreshParcelIcons()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    // Our "cursor" moving right to left
    S32 x = mAddLandmarkBtn->getRect().mLeft;

    LLViewerParcelMgr* vpm = LLViewerParcelMgr::getInstance();

    LLViewerRegion* agent_region = gAgent.getRegion();
    LLParcel* agent_parcel = vpm->getAgentParcel();
    if (!agent_region || !agent_parcel)
        return;

    mForSaleBtn->setVisible(vpm->canAgentBuyParcel(agent_parcel, false));

    x = layout_widget(mForSaleBtn, x);

    static LLCachedControl<bool> show_icons(gSavedSettings, "NavBarShowParcelProperties", false);
    mParcelIcons.update(show_icons);

    if (show_icons)
    {
        // Padding goes to left of both landmark star and for sale btn
        x -= mAddLandmarkHPad;
        x = mParcelIcons.layout(x, ALParcelIconStrip::LAYOUT_LEFTWARD, mIconHPad);
    }

    if (mTextEntry)
    {
        S32 left_pad, right_pad;
        mTextEntry->getTextPadding(&left_pad, &right_pad);
        right_pad = mTextEntry->getRect().mRight - x;
        mTextEntry->setTextPadding(left_pad, right_pad);
    }
}


void LLLocationInputCtrl::updateMaturityButtonImage()
{
    LLViewerRegion* region = gAgent.getRegion();
    if (!region)
        return;

    U8 sim_access = region->getSimAccess();
    if (mLastSimAccess == sim_access)
    {
        return;
    }
    mLastSimAccess = sim_access;

    LLPointer<LLUIImage> rating_image = NULL;
    std::string rating_tooltip;

    switch(sim_access)
    {
    case SIM_ACCESS_PG:
        rating_image = mIconMaturityGeneral;
        rating_tooltip = LLTrans::getString("LocationCtrlGeneralIconTooltip");
        break;

    case SIM_ACCESS_ADULT:
        rating_image = mIconMaturityAdult;
        rating_tooltip = LLTrans::getString("LocationCtrlAdultIconTooltip");
        break;

    case SIM_ACCESS_MATURE:
        rating_image = mIconMaturityModerate;
        rating_tooltip = LLTrans::getString("LocationCtrlModerateIconTooltip");
        break;

    default:
        // No icon for this rating, so there is nothing to place either.
        mMaturityRatingShown = false;
        mMaturityButton->setVisible(false);
        return;
    }

    mMaturityRatingShown = true;
    mMaturityButton->setToolTip(rating_tooltip);
    mMaturityButton->setImageUnselected(rating_image);
    mMaturityButton->setImagePressed(rating_image);

    positionMaturityButton();
}

void LLLocationInputCtrl::positionMaturityButton()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    // Whether this region's rating has an icon at all, which is not the same
    // question as whether the button is on screen: the last line of this
    // function hides it when the text leaves no room, and reading that back as
    // the gate is what used to keep the icon hidden after the field was made
    // wide again.
    if (!mMaturityRatingShown)
    {
        return;
    }

    const LLFontGL* font = mTextEntry->getFont();
    if (!font)
        return;

    S32 left_pad, right_pad;
    mTextEntry->getTextPadding(&left_pad, &right_pad);

    // Calculate the right edge of rendered text + a whitespace.
    //
    // Through the cache, naming the field and its own version counter as the
    // source: a reshape asks this of text that has not changed, and does so on
    // every frame of a drag. The trailing space is left uncached -- it is one
    // glyph, the shaping cache answers it, and a second slot keyed on a
    // different string would break this cache's one-source rule.
    mMaturityWidthCache.setSource(mTextEntry, mTextEntry->getTextGeneration());
    const S32 text_width = llceil(mMaturityWidthCache.getWidthBytes(
        font, mTextEntry->getText(), 0, S32_MAX, false));
    left_pad = left_pad + text_width + font->getWidth(" ");

    LLRect rect = mMaturityButton->getRect();
    mMaturityButton->setRect(rect.setOriginAndSize(left_pad, rect.mBottom, rect.getWidth(), rect.getHeight()));

    // Hide icon if it text area is not width enough to display it, show otherwise.
    mMaturityButton->setVisible(rect.mRight < mTextEntry->getRect().getWidth() - right_pad);
}

void LLLocationInputCtrl::addLocationHistoryEntry(const std::string& title, const LLSD& value)
{
    // SL-20286 : Duplication of autocomplete results occurs when entering some search queries in the navigation bar
    // Exclude visual duplicates (items with the same titles) in the dropdown list
    LLScrollListItem* item = mList->getItemByLabel(title);
    if (!item)
    {
        add(title, value);
    }
}

void LLLocationInputCtrl::rebuildLocationHistory(const std::string& filter)
{
    LLLocationHistory::location_list_t filtered_items;
    const LLLocationHistory::location_list_t* itemsp = NULL;
    LLLocationHistory* lh = LLLocationHistory::getInstance();

    if (filter.empty())
    {
        itemsp = &lh->getItems();
    }
    else
    {
        lh->getMatchingItems(filter, filtered_items);
        itemsp = &filtered_items;
    }

    removeall();
    for (LLLocationHistory::location_list_t::const_reverse_iterator it = itemsp->rbegin(); it != itemsp->rend(); it++)
    {
        LLSD value;
        value["tooltip"] = it->getToolTip();
        //location history can contain only typed locations
        value["item_type"] = TYPED_REGION_SLURL;
        value["global_pos"] = it->mGlobalPos.getValue();
        addLocationHistoryEntry(it->getLocation(), value);
    }
}

void LLLocationInputCtrl::focusTextEntry()
{
    // We can't use "mTextEntry->setFocus(true)" instead because
    // if the "select_on_focus" parameter is true it places the cursor
    // at the beginning (after selecting text), thus screwing up updateSelection().
    if (mTextEntry)
    {
        gFocusMgr.setKeyboardFocus(mTextEntry);

        // Enable the text entry to handle accelerator keys (EXT-8104).
        LLEditMenuHandler::gEditMenuHandler = mTextEntry;
    }
}

void LLLocationInputCtrl::enableAddLandmarkButton(bool val)
{
    // We don't want to disable the button because it should be click able at any time,
    // instead switch images.
    LLUIImage* img = val ? mLandmarkImageOn : mLandmarkImageOff;
    if(img)
    {
        mAddLandmarkBtn->setImageUnselected(img);
    }
}

// Change the "Add landmark" button image
// depending on whether current parcel has been landmarked.
void LLLocationInputCtrl::updateAddLandmarkButton()
{
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
    mAddLandmarkBtn->setVisible(!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC));
// [/RLVa:KB]
    enableAddLandmarkButton(LLLandmarkActions::hasParcelLandmark());
}
void LLLocationInputCtrl::updateAddLandmarkTooltip()
{
    std::string tooltip;
    if(LLLandmarkActions::landmarkAlreadyExists())
    {
        tooltip = mEditLandmarkTooltip;
    }
    else
    {
        tooltip = mAddLandmarkTooltip;
    }
    mAddLandmarkBtn->setToolTip(tooltip);
}

void LLLocationInputCtrl::updateContextMenu(){

    if (mLocationContextMenu)
    {
        LLMenuItemGL* landmarkItem = mLocationContextMenu->getChild<LLMenuItemGL>("Landmark");
        if (!LLLandmarkActions::landmarkAlreadyExists())
        {
            landmarkItem->setLabel(LLTrans::getString("AddLandmarkNavBarMenu"));
        }
        else
        {
            landmarkItem->setLabel(LLTrans::getString("EditLandmarkNavBarMenu"));
        }
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
        landmarkItem->setEnabled(!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC));
// [/RLVa:KB]
    }
}
void LLLocationInputCtrl::updateWidgetlayout()
{
    const LLRect&   rect            = getLocalRect();
    const LLRect&   hist_btn_rect   = mButton->getRect();

    // Info button is set in the XUI XML location_input.xml

    // "Add Landmark" button
    LLRect al_btn_rect = mAddLandmarkBtn->getRect();
    al_btn_rect.translate(
        hist_btn_rect.mLeft - mIconHPad - al_btn_rect.getWidth(),
        (rect.getHeight() - al_btn_rect.getHeight()) / 2);
    mAddLandmarkBtn->setRect(al_btn_rect);
}

void LLLocationInputCtrl::changeLocationPresentation()
{
    if (!mTextEntry)
        return;

    //change location presentation only if user does not select/paste anything and
    //human-readable region name is being displayed
    if(!mTextEntry->hasSelection() && mTextEntry->getText() == mHumanReadableLocation)
    {
        //needs unescaped one
        LLSLURL slurl;
        LLAgentUI::buildSLURL(slurl, false);
        mTextEntry->setText(LLURI::unescape(slurl.getSLURLString()));
        mTextEntry->selectAll();

        mMaturityButton->setVisible(false);

        isHumanReadableLocationVisible = false;
    }
}

void LLLocationInputCtrl::onLocationContextMenuItemClicked(const LLSD& userdata)
{
    std::string item = userdata.asString();

    if (item == "show_coordinates")
    {
        gSavedSettings.setBOOL("NavBarShowCoordinates",!gSavedSettings.getBOOL("NavBarShowCoordinates"));
    }
    else if (item == "show_properties")
    {
        gSavedSettings.setBOOL("NavBarShowParcelProperties",
            !gSavedSettings.getBOOL("NavBarShowParcelProperties"));
    }
    else if (item == "landmark")
    {
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.4.5) | Added: RLVa-1.2.0
        if (!gRlvHandler.hasBehaviour(RLV_BHVR_SHOWLOC))
        {
// [/RLVa:KB]
            LLViewerInventoryItem* landmark = LLLandmarkActions::findLandmarkForAgentPos();

            if(!landmark)
            {
                LLFloaterReg::showInstance("add_landmark");
            }
            else
            {
                LLFloaterSidePanelContainer::showPanel("places", LLSD().with("type", "landmark").with("id",landmark->getUUID()));
            }
// [RLVa:KB] - Checked: 2010-04-05 (RLVa-1.2.0d) | Added: RLVa-1.2.0d
        }
// [/RLVa:KB]
    }
    else if (item == "cut")
    {
        mTextEntry->cut();
    }
    else if (item == "copy")
    {
        mTextEntry->copy();
    }
    else if (item == "paste")
    {
        mTextEntry->paste();
    }
    else if (item == "delete")
    {
        mTextEntry->deleteSelection();
    }
    else if (item == "select_all")
    {
        mTextEntry->selectAll();
    }
}

bool LLLocationInputCtrl::onLocationContextMenuItemEnabled(const LLSD& userdata)
{
    std::string item = userdata.asString();

    if (item == "can_cut")
    {
        return mTextEntry->canCut();
    }
    else if (item == "can_copy")
    {
        return mTextEntry->canCopy();
    }
    else if (item == "can_paste")
    {
        return mTextEntry->canPaste();
    }
    else if (item == "can_delete")
    {
        return mTextEntry->canDeselect();
    }
    else if (item == "can_select_all")
    {
        return mTextEntry->canSelectAll() && (mTextEntry->getLengthBytes() > 0);
    }
    else if(item == "show_coordinates")
    {
        return gSavedSettings.getBOOL("NavBarShowCoordinates");
    }

    return false;
}

void LLLocationInputCtrl::callbackRebakeRegion(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
    if (option == 0) // OK
    {
        if (LLPathfindingManager::getInstance() != NULL)
        {
            LLMenuOptionPathfindingRebakeNavmesh::getInstance()->sendRequestRebakeNavmesh();
        }
    }
}

void LLLocationInputCtrl::onPathfindingIconClick(ALParcelIconStrip::EIcon icon)
{
    if (icon == ALParcelIconStrip::ICON_PATHFINDING_DISABLED)
    {
        LLNotificationsUtil::add("DynamicPathfindingDisabled");
        return;
    }

    if (LLPathfindingManager::getInstance() != NULL)
    {
        LLMenuOptionPathfindingRebakeNavmesh *rebakeInstance = LLMenuOptionPathfindingRebakeNavmesh::getInstance();
        if (rebakeInstance && rebakeInstance->canRebakeRegion() && (rebakeInstance->getMode() == LLMenuOptionPathfindingRebakeNavmesh::kRebakeNavMesh_Available))
        {
            LLNotificationsUtil::add("PathfindingDirtyRebake", LLSD(), LLSD(),
                                     boost::bind(&LLLocationInputCtrl::callbackRebakeRegion, this, _1, _2));
            return;
        }
    }
    LLNotificationsUtil::add("PathfindingDirty");
}

void LLLocationInputCtrl::createNavMeshStatusListenerForCurrentRegion()
{
    if (mNavMeshSlot.connected())
    {
        mNavMeshSlot.disconnect();
    }

    LLViewerRegion *currentRegion = gAgent.getRegion();
    if (currentRegion != NULL)
    {
        mNavMeshSlot = LLPathfindingManager::getInstance()->registerNavMeshListenerForRegion(currentRegion, boost::bind(&LLLocationInputCtrl::onNavMeshStatusChange, this, _2));
        LLPathfindingManager::getInstance()->requestGetNavMeshForRegion(currentRegion, true);
    }
}
