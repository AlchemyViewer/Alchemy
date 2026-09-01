/**
* @file llstatusbar.cpp
* @brief LLStatusBar class implementation
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

#include "llstatusbar.h"

// viewer includes
#include "alpanelaopulldown.h"
#include "alpanelquicksettingspulldown.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llbutton.h"
#include "llcommandhandler.h"
#include "llfirstuse.h"
#include "llviewercontrol.h"
#include "llfloaterbuycurrency.h"
#include "llbuycurrencyhtml.h"
#include "llpanelnearbymedia.h"
#include "llpanelpresetscamerapulldown.h"
#include "llpanelpresetspulldown.h"
#include "llpanelvolumepulldown.h"
#include "llpanelpulldown.h"
#include "llfloatermarketplace.h"
#include "llfloaterregioninfo.h"
#include "llfloaterscriptdebug.h"
#include "llhints.h"
#include "llhudicon.h"
#include "llnavigationbar.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llrootview.h"
#include "llsd.h"
#include "lltextbox.h"
#include "llui.h"
#include "llviewerparceloverlay.h"
#include "llviewerregion.h"
#include "llviewerstats.h"
#include "llviewerwindow.h"
#include "llframetimer.h"
#include "llvoavatarself.h"
#include "llresmgr.h"
#include "llworld.h"
#include "llstatgraph.h"
#include "llviewermedia.h"
#include "llviewermenu.h"   // for gMenuBarView
#include "llviewerparcelmgr.h"
#include "llviewerthrottle.h"
#include "lluictrlfactory.h"

#include "lltoolmgr.h"
#include "llfocusmgr.h"
#include "llappviewer.h"
#include "lltrans.h"

// library includes
#include "llfloaterreg.h"
#include "llfontgl.h"
#include "llrect.h"
#include "llerror.h"
#include "llnotificationsutil.h"
#include "llparcel.h"
#include "llstring.h"
#include "message.h"
#include "llsearchableui.h"
#include "llsearcheditor.h"

#include <fmt/format.h>

// system includes
#include <iomanip>

//
// Globals
//
LLStatusBar *gStatusBar = NULL;
S32 STATUS_BAR_HEIGHT = 26;
extern S32 MENU_BAR_HEIGHT;


static void onClickVolume(void*);

LLStatusBar::LLStatusBar(const LLRect& rect)
:   LLPanel(),
    mTextTime(NULL),
    mBtnVolume(NULL),
    mBoxBalance(NULL),
    mBalance(0),
    mBalanceClicked(false),
    mObscureBalance(false),
    mSquareMetersCredit(0),
    mSquareMetersCommitted(0),
    mFilterEdit(NULL),          // Edit for filtering
    mSearchPanel(NULL)          // Panel for filtering
{
    setRect(rect);

    // status bar can possible overlay menus?
    setMouseOpaque(false);

    buildFromFile("panel_status_bar.xml");
}

LLStatusBar::~LLStatusBar()
{
    // LLView destructor cleans up children
}

//-----------------------------------------------------------------------
// Overrides
//-----------------------------------------------------------------------

// virtual
void LLStatusBar::draw()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    refresh();
    LLPanel::draw();
}

bool LLStatusBar::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    show_navbar_context_menu(this,x,y);
    return true;
}

bool LLStatusBar::postBuild()
{
    gMenuBarView->setRightMouseDownCallback(boost::bind(&show_navbar_context_menu, _1, _2, _3));

    mPanelPopupHolder = gViewerWindow->getRootView()->getChildView("popup_holder");

    mTextTime = getChild<LLTextBox>("TimeText" );
    mTextTime->setClickedCallback(boost::bind(&LLStatusBar::onClickToggleClockStyle, this));

    getChild<LLUICtrl>("buyL")->setCommitCallback(
        boost::bind(&LLStatusBar::onClickBuyCurrency, this));

    getChild<LLUICtrl>("goShop")->setCommitCallback(
        boost::bind(&LLStatusBar::onClickShop, this));

    mBoxBalance = getChild<LLTextBox>("balance");
    mBoxBalance->setClickedCallback(&LLStatusBar::onClickRefreshBalance, this);
    mBoxBalance->setDoubleClickCallback([this](LLUICtrl*, S32 x, S32 y, MASK mask) { onClickToggleBalance(); });

    mIconPresetsCamera = getChild<LLButton>( "presets_icon_camera" );
    mIconPresetsCamera->setMouseEnterCallback(boost::bind(&LLStatusBar::onMouseEnterPresetsCamera, this));

    mIconPresetsGraphic = getChild<LLButton>( "presets_icon_graphic" );
    mIconPresetsGraphic->setMouseEnterCallback(boost::bind(&LLStatusBar::onMouseEnterPresets, this));

    mBtnQuickSettings = getChild<LLButton>("quick_settings_btn");
    mBtnQuickSettings->setMouseEnterCallback(boost::bind(&LLStatusBar::onMouseEnterQuickSettings, this));

    mBtnAO = getChild<LLButton>("ao_btn");
    mBtnAO->setClickedCallback(&LLStatusBar::onClickAOBtn, this);
    mBtnAO->setMouseEnterCallback(boost::bind(&LLStatusBar::onMouseEnterAO, this));
    mBtnAO->setToggleState(gSavedPerAccountSettings.getBOOL("AlchemyAOEnable")); // shunt it into correct state - ALCH-368

    mBtnVolume = getChild<LLButton>( "volume_btn" );
    mBtnVolume->setClickedCallback( onClickVolume, this );
    mBtnVolume->setMouseEnterCallback(boost::bind(&LLStatusBar::onMouseEnterVolume, this));
    // The per-frame poll that used to write this is gone, so the button starts
    // out of step with a setting restored from the last session unless it is
    // shunted into state once here.
    mBtnVolume->setToggleState(LLAppViewer::instance()->getMasterSystemAudioMute());

    mMediaToggle = getChild<LLButton>("media_toggle_btn");
    mMediaToggle->setClickedCallback( &LLStatusBar::onClickMediaToggle, this );
    mMediaToggle->setMouseEnterCallback(boost::bind(&LLStatusBar::onMouseEnterNearbyMedia, this));

    mBalanceBG = getChild<LLView>("balance_bg");
    LLHints::getInstance()->registerHintTarget("linden_balance", mBalanceBG->getHandle());

    gSavedSettings.getControl("MuteAudio")->getSignal()->connect(boost::bind(&LLStatusBar::onVolumeChanged, this, _2));
    gSavedSettings.getControl("EnableVoiceChat")->getSignal()->connect(boost::bind(&LLStatusBar::onVoiceChanged, this, _2));
    gSavedSettings.getControl("ObscureBalanceInStatusBar")->getSignal()->connect(boost::bind(&LLStatusBar::onObscureBalanceChanged, this, _2));
    gSavedSettings.getControl("Use24HourClockInStatusBar")->getSignal()->connect(boost::bind(&LLStatusBar::updateClock, this));
    gSavedSettings.getControl("ShowStatusBarSeconds")->getCommitSignal()->connect(boost::bind(&LLStatusBar::updateClock, this));
    gSavedSettings.getControl("ShowStatusBarTime")->getCommitSignal()->connect(boost::bind(&LLStatusBar::updateClock, this));

    if (!gSavedSettings.getBOOL("EnableVoiceChat") && LLAppViewer::instance()->isSecondInstance())
    {
        // Indicate that second instance started without sound
        mBtnVolume->setImageUnselected(LLUI::getUIImage("VoiceMute_Off"));
    }
    mObscureBalance = gSavedSettings.getBOOL("ObscureBalanceInStatusBar");

    mTextFPS = getChild<LLTextBox>("FPSText");
    mTextFPS->setClickedCallback([](void*) { LLFloaterReg::showInstance("stats"); });

    mTextFPS->setVisible(gSavedSettings.getBOOL("ShowStatusBarFPS"));

    updateBalancePanelPosition();

    // Hook up and init for filtering
    mFilterEdit = getChild<LLSearchEditor>( "search_menu_edit" );
    mSearchPanel = getChild<LLPanel>( "menu_search_panel" );

    bool search_panel_visible = gSavedSettings.getBOOL("MenuSearch");
    mSearchPanel->setVisible(search_panel_visible);
    mFilterEdit->setKeystrokeCallback(boost::bind(&LLStatusBar::onUpdateFilterTerm, this));
    mFilterEdit->setCommitCallback(boost::bind(&LLStatusBar::onUpdateFilterTerm, this));
    collectSearchableItems();
    gSavedSettings.getControl("MenuSearch")->getCommitSignal()->connect(boost::bind(&LLStatusBar::updateMenuSearchVisibility, this, _2));

    if (search_panel_visible)
    {
        updateMenuSearchPosition();
    }

    updateClock();

    return true;
}

// Per-frame updates of visibility
void LLStatusBar::refresh()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    // update clock every 10 seconds
    static LLCachedControl<bool> show_clock(gSavedSettings, "ShowStatusBarTime", false);
    static LLCachedControl<bool> show_clock_seconds(gSavedSettings, "ShowStatusBarSeconds", false);
    if (show_clock && (mClockUpdateTimer.getElapsedTimeF32() > 10.f || (show_clock_seconds && mClockUpdateTimer.getElapsedTimeF32() > 1.f)))
    {
        mClockUpdateTimer.reset();

        updateClock();
    }

    if (mBalanceClicked && mBalanceClickTimer.getElapsedTimeF32() > 1.f)
    {
        mBalanceClicked = false;
        sendMoneyBalanceRequest();
    }

    // Reshape the menu bar to its content's width. Kept as a poll: the bar
    // does arrange itself when it is told its items moved, but the only thing
    // that tells it is the menu search filter, and this catches whatever else
    // hides a top-level menu without saying so. The walk stops at the first
    // visible item from the right, so it costs an int compare.
    const S32 MENU_RIGHT = gMenuBarView->getRightmostMenuEdge();
    if (MENU_RIGHT != gMenuBarView->getRect().getWidth())
    {
        gMenuBarView->reshape(MENU_RIGHT, gMenuBarView->getRect().getHeight());
    }

    // What the media button says is worth asking about several times a second,
    // not several times a frame. Answering it walks every media impl in the
    // scene and asks the parcel for two URLs, one of which is assembled from a
    // MIME type comparison -- and the answer decides whether a button is
    // enabled and which of two images it shows. The clock above and the frame
    // counter below are metered for the same reason.
    if (mMediaUpdateTimer.getElapsedTimeF32() > 0.2f)
    {
        mMediaUpdateTimer.reset();

        LLViewerMedia* media_inst = LLViewerMedia::getInstance();

        // Disable media toggle if there's no media, parcel media, and no parcel audio
        // (or if media is disabled)
        static const LLCachedControl<bool> audio_streaming_enabled(gSavedSettings, "AudioStreamingMusic");
        static const LLCachedControl<bool> media_streaming_enabled(gSavedSettings, "AudioStreamingMedia");
        bool button_enabled = (audio_streaming_enabled || media_streaming_enabled) &&
                              (media_inst->hasInWorldMedia() || media_inst->hasParcelMedia() || media_inst->hasParcelAudio());
        mMediaToggle->setEnabled(button_enabled);
        // Note the "sense" of the toggle is opposite whether media is playing or not
        bool any_media_playing = (media_inst->isAnyMediaPlaying() ||
                                  media_inst->isParcelMediaPlaying() ||
                                  media_inst->isParcelAudioPlaying());
        mMediaToggle->setValue(!any_media_playing);
    }

    static LLCachedControl<bool> show_fps(gSavedSettings, "ShowStatusBarFPS", false);
    if (show_fps && mFPSUpdateTimer.getElapsedTimeF32() > 0.1f)
    {
        mFPSUpdateTimer.reset();
        F32 fps = (F32)LLTrace::get_frame_recording().getLastRecording().getMean(LLStatViewer::FPS_SAMPLE);
        mTextFPS->setText(fmt::format("{:d}", ll_round(fps)));
    }
}

void LLStatusBar::setVisibleForMouselook(bool visible)
{
    static LLCachedControl<bool> show_fps(gSavedSettings, "ShowStatusBarFPS", false);
    static LLCachedControl<bool> show_menu_search(gSavedSettings, "MenuSearch", false);
    mTextTime->setVisible(visible);
    mBalanceBG->setVisible(visible);
    mBoxBalance->setVisible(visible);
    mBtnQuickSettings->setVisible(visible);
    mBtnAO->setVisible(visible);
    mBtnVolume->setVisible(visible);
    mMediaToggle->setVisible(visible);
    mSearchPanel->setVisible(visible && show_menu_search);
    setBackgroundVisible(visible);
    mIconPresetsCamera->setVisible(visible);
    mIconPresetsGraphic->setVisible(visible);
    mTextFPS->setVisible(visible && show_fps);
}

void LLStatusBar::debitBalance(S32 debit)
{
    setBalance(getBalance() - debit);
}

void LLStatusBar::creditBalance(S32 credit)
{
    setBalance(getBalance() + credit);
}

void LLStatusBar::setBalance(S32 balance)
{
    if (balance > getBalance() && getBalance() != 0)
    {
        LLFirstUse::receiveLindens();
    }

    std::string money_str = LLResMgr::getInstance()->getMonetaryString( balance );

    LLStringUtil::format_map_t string_args;
    if (mObscureBalance)
    {
        string_args["[AMT]"] = "****";
    }
    else
    {
        string_args["[AMT]"] = money_str;
    }
    std::string label_str = getString("buycurrencylabel", string_args);
    mBoxBalance->setValue(label_str);
    mBoxBalance->setToolTipArg(LLStringExplicit("[AMT]"), money_str);

    updateBalancePanelPosition();

    // If the search panel is shown, move this according to the new balance width. Parcel text will reshape itself in setParcelInfoText
    if (mSearchPanel && mSearchPanel->getVisible())
    {
        updateMenuSearchPosition();
    }

    if (mBalance && (fabs((F32)(mBalance - balance)) > gSavedSettings.getF32("UISndMoneyChangeThreshold")))
    {
        if (mBalance > balance)
            make_ui_sound("UISndMoneyChangeDown");
        else
            make_ui_sound("UISndMoneyChangeUp");
    }

    if( balance != mBalance )
    {
        mBalance = balance;
    }
}


// static
void LLStatusBar::sendMoneyBalanceRequest()
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MoneyBalanceRequest);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_MoneyData);
    msg->addUUIDFast(_PREHASH_TransactionID, LLUUID::null );

    if (gDisconnected)
    {
        LL_DEBUGS() << "Trying to send message when disconnected, skipping balance request!" << LL_ENDL;
        return;
    }
    if (!gAgent.getRegion())
    {
        LL_DEBUGS() << "LLAgent::sendReliableMessage No region for agent yet, skipping balance request!" << LL_ENDL;
        return;
    }
    // Double amount of retries due to this request initially happening during busy stage
    // Ideally this should be turned into a capability
    gMessageSystem->sendReliable(gAgent.getRegionHost(), LL_DEFAULT_RELIABLE_RETRIES * 2, true, LL_PING_BASED_TIMEOUT_DUMMY, NULL, NULL);
}


S32 LLStatusBar::getBalance() const
{
    return mBalance;
}


void LLStatusBar::setLandCredit(S32 credit)
{
    mSquareMetersCredit = credit;
}
void LLStatusBar::setLandCommitted(S32 committed)
{
    mSquareMetersCommitted = committed;
}

bool LLStatusBar::isUserTiered() const
{
    return (mSquareMetersCredit > 0);
}

S32 LLStatusBar::getSquareMetersCredit() const
{
    return mSquareMetersCredit;
}

S32 LLStatusBar::getSquareMetersCommitted() const
{
    return mSquareMetersCommitted;
}

S32 LLStatusBar::getSquareMetersLeft() const
{
    return mSquareMetersCredit - mSquareMetersCommitted;
}

void LLStatusBar::onClickBuyCurrency()
{
    // open a currency floater - actual one open depends on
    // value specified in settings.xml
    LLBuyCurrencyHTML::openCurrencyFloater();
    LLFirstUse::receiveLindens(false);
}

void LLStatusBar::onClickShop()
{
    LLFloaterReg::showInstanceOrBringToFront("marketplace");
    if (LLFloaterMarketplace* marketplace = LLFloaterReg::getTypedInstance<LLFloaterMarketplace>("marketplace"))
    {
        marketplace->openMarketplace();
    }
}

template <typename T>
T* LLStatusBar::ensurePulldown(T*& slot)
{
    if (!slot)
    {
        slot = new T();
        addChild(slot);
        slot->setFollows(FOLLOWS_TOP | FOLLOWS_RIGHT);
        slot->setVisible(false);
    }
    return slot;
}

void LLStatusBar::showPulldown(LLPanelPulldown* shown, const LLView* anchor, bool centered)
{
    const LLRect anchor_rect = anchor->getRect();
    LLRect rect = shown->getRect();

    // How much wider the panel is than the button it hangs off: shared with
    // the button's left edge, or split either side of it.
    const S32 overhang = rect.getWidth() - anchor_rect.getWidth();
    rect.setLeftTopAndSize(anchor_rect.mLeft - (centered ? overhang / 2 : overhang),
                           anchor_rect.mBottom,
                           rect.getWidth(),
                           rect.getHeight());
    // force onscreen
    rect.translate(mPanelPopupHolder->getRect().getWidth() - rect.mRight, 0);
    shown->setShape(rect);

    LLUI::getInstance()->clearPopups();
    LLUI::getInstance()->addPopup(shown);

    // Only the ones that exist: a pull-down nobody has hovered has not been
    // built, and one that was never built is not showing.
    for (LLPanelPulldown* other : { static_cast<LLPanelPulldown*>(mPanelPresetsCameraPulldown),
                                    static_cast<LLPanelPulldown*>(mPanelPresetsPulldown),
                                    static_cast<LLPanelPulldown*>(mPanelQuickSettingsPulldown),
                                    static_cast<LLPanelPulldown*>(mPanelAOPulldown),
                                    static_cast<LLPanelPulldown*>(mPanelVolumePulldown),
                                    static_cast<LLPanelPulldown*>(mPanelNearByMedia) })
    {
        if (other && other != shown)
        {
            other->setVisible(false);
        }
    }
    shown->setVisible(true);
}

void LLStatusBar::onMouseEnterPresetsCamera()
{
    showPulldown(ensurePulldown(mPanelPresetsCameraPulldown), mIconPresetsCamera, false);
}

void LLStatusBar::onMouseEnterPresets()
{
    showPulldown(ensurePulldown(mPanelPresetsPulldown), mIconPresetsGraphic, false);
}

void LLStatusBar::onMouseEnterQuickSettings()
{
    showPulldown(ensurePulldown(mPanelQuickSettingsPulldown), mBtnQuickSettings, true);
}

void LLStatusBar::onMouseEnterAO()
{
    showPulldown(ensurePulldown(mPanelAOPulldown), mBtnAO, true);
}

void LLStatusBar::onMouseEnterVolume()
{
    showPulldown(ensurePulldown(mPanelVolumePulldown), mBtnVolume, false);
}

void LLStatusBar::onMouseEnterNearbyMedia()
{
    showPulldown(ensurePulldown(mPanelNearByMedia), mMediaToggle, true);
}


// static
void LLStatusBar::onClickAOBtn(void* data)
{
    gSavedPerAccountSettings.set("AlchemyAOEnable", !gSavedPerAccountSettings.getBOOL("AlchemyAOEnable"));
}

static void onClickVolume(void* data)
{
    // toggle the master mute setting
    bool mute_audio = LLAppViewer::instance()->getMasterSystemAudioMute();
    LLAppViewer::instance()->setMasterSystemAudioMute(!mute_audio);
}

//static
void LLStatusBar::onClickRefreshBalance(void* data)
{
    LLStatusBar* status_bar = (LLStatusBar*)data;

    if (!status_bar->mBalanceClicked)
    {
        // Schedule a balance request message:
        status_bar->mBalanceClicked = true;
        status_bar->mBalanceClickTimer.reset();
    }
    // The refresh of the display (call to setBalance()) will be done by process_money_balance_reply()
}

void LLStatusBar::onClickToggleBalance()
{
    mObscureBalance = !mObscureBalance;
    gSavedSettings.setBOOL("ObscureBalanceInStatusBar", mObscureBalance);
    setBalance(mBalance);
    mBalanceClicked = false; // supress click
}

//static
void LLStatusBar::onClickMediaToggle(void* data)
{
    LLStatusBar *status_bar = (LLStatusBar*)data;
    // "Selected" means it was showing the "play" icon (so media was playing), and now it shows "pause", so turn off media
    bool pause = status_bar->mMediaToggle->getValue();
    LLViewerMedia::getInstance()->setAllMediaPaused(pause);
}

void LLStatusBar::onAOStateChanged()
{
    mBtnAO->setToggleState(gSavedPerAccountSettings.getBOOL("AlchemyAOEnable"));
}

bool can_afford_transaction(S32 cost)
{
    return((cost <= 0)||((gStatusBar) && (gStatusBar->getBalance() >=cost)));
}

void LLStatusBar::onVolumeChanged(const LLSD& newvalue)
{
    // The setting this rides on is the one the button reports, so the button
    // is written here rather than read back out of the settings store on every
    // frame. Both ways the value moves -- the button itself, and a script or
    // the menu -- end up at this control, so both end up here.
    mBtnVolume->setToggleState(newvalue.asBoolean());
}

void LLStatusBar::onVoiceChanged(const LLSD& newvalue)
{
    if (newvalue.asBoolean())
    {
        // Second instance starts with "VoiceMute_Off" icon, fix it
        mBtnVolume->setImageUnselected(LLUI::getUIImage("Audio_Off"));
    }
}

void LLStatusBar::onObscureBalanceChanged(const LLSD& newvalue)
{
    mObscureBalance = newvalue.asBoolean();
    setBalance(mBalance);
}

void LLStatusBar::onUpdateFilterTerm()
{
    const std::string searchValue = utf8str_tolower( mFilterEdit->getValue().asString() );

    if( !mSearchData || mSearchData->mLastFilter == searchValue )
        return;

    mSearchData->mLastFilter = searchValue;

    mSearchData->mRootMenu->hightlightAndHide( searchValue );
    gMenuBarView->needsArrange();
}

void collectChildren( LLMenuGL *aMenu, ll::statusbar::SearchableItemPtr aParentMenu )
{
    for( U32 i = 0; i < aMenu->getItemCount(); ++i )
    {
        LLMenuItemGL *pMenu = aMenu->getItem( i );

        ll::statusbar::SearchableItemPtr pItem = std::make_shared<ll::statusbar::SearchableItem>();
        pItem->mCtrl = pMenu;
        pItem->mMenu = pMenu;
        pItem->mLabel = utf8str_tolower( pMenu->ll::ui::SearchableControl::getSearchText() );
        aParentMenu->mChildren.push_back( pItem );

        LLMenuItemBranchGL *pBranch = dynamic_cast< LLMenuItemBranchGL* >( pMenu );
        if( pBranch )
            collectChildren( pBranch->getBranch(), pItem );
    }

}

void LLStatusBar::collectSearchableItems()
{
    mSearchData = std::make_unique<ll::statusbar::SearchData>();
    ll::statusbar::SearchableItemPtr pItem = std::make_shared<ll::statusbar::SearchableItem>();
    mSearchData->mRootMenu = pItem;
    collectChildren( gMenuBarView, pItem );
}

void LLStatusBar::updateMenuSearchVisibility(const LLSD& data)
{
    bool visible = data.asBoolean();
    mSearchPanel->setVisible(visible);
    if (!visible)
    {
        mFilterEdit->setText(LLStringUtil::null);
        onUpdateFilterTerm();
    }
    else
    {
        updateMenuSearchPosition();
    }
}

void LLStatusBar::updateMenuSearchPosition()
{
    const S32 HPAD = 12;
    LLRect balanceRect = mBalanceBG->getRect();
    LLRect searchRect = mSearchPanel->getRect();
    S32 w = searchRect.getWidth();
    searchRect.mLeft = balanceRect.mLeft - w - HPAD;
    searchRect.mRight = searchRect.mLeft + w;
    mSearchPanel->setShape( searchRect );
}

void LLStatusBar::updateBalancePanelPosition()
{
    // Resize the L$ balance background to be wide enough for your balance plus the buy button
    const S32 HPAD = 24;
    LLRect balance_rect = mBoxBalance->getTextBoundingRect();
    LLRect buy_rect = getChildView("buyL")->getRect();
    LLRect shop_rect = getChildView("goShop")->getRect();
    LLRect balance_bg_rect = mBalanceBG->getRect();
    balance_bg_rect.mLeft = balance_bg_rect.mRight - (buy_rect.getWidth() + shop_rect.getWidth() + balance_rect.getWidth() + HPAD);
    mBalanceBG->setShape(balance_bg_rect);
}

void LLStatusBar::setBalanceVisible(bool visible)
{
    mBoxBalance->setVisible(visible);
}

void LLStatusBar::updateClock()
{
    static LLCachedControl<bool> use_24h(gSavedSettings, "Use24HourClockInStatusBar", false);
    static LLCachedControl<bool> show_clock_seconds(gSavedSettings, "ShowStatusBarSeconds", false);

    // Get current UTC time, adjusted for the user's clock
    // being off.
    time_t utc_time;
    utc_time = time_corrected();

    std::string timeStr = show_clock_seconds ?
    use_24h ? getString("time_sec") : getString("time_ampm_sec") :
    use_24h ? getString("time") : getString("time_ampm");

    LLSD substitution;
    substitution["datetime"] = (S32) utc_time;
    LLStringUtil::format (timeStr, substitution);
    mTextTime->setText(timeStr);

    // set the tooltip to have the date
    std::string dtStr = getString("timeTooltip");
    LLStringUtil::format (dtStr, substitution);
    mTextTime->setToolTip (dtStr);
}

void LLStatusBar::onClickToggleClockStyle()
{
    gSavedSettings.setBOOL("Use24HourClockInStatusBar", !gSavedSettings.getBOOL("Use24HourClockInStatusBar"));
    updateClock();
}

// Implements secondlife:///app/balance/request to request a L$ balance
// update via UDP message system. JC
class LLBalanceHandler : public LLCommandHandler
{
public:
    // Requires "trusted" browser/URL source
    LLBalanceHandler() : LLCommandHandler("balance", UNTRUSTED_BLOCK) { }
    bool handle(const LLSD& tokens, const LLSD& query_map, const std::string& grid, LLMediaCtrl* web)
    {
        if (tokens.size() == 1
            && tokens[0].asString() == "request")
        {
            LLStatusBar::sendMoneyBalanceRequest();
            return true;
        }
        return false;
    }
};
// register with command dispatch system
LLBalanceHandler gBalanceHandler;
