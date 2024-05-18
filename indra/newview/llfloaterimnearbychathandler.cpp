/**
 * @file LLFloaterIMNearbyChatHandler.cpp
 * @brief Nearby chat chat managment
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

#include "llagentdata.h" // for gAgentID
#include "llfloaterimnearbychathandler.h"

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
#include "llchatentry.h"
// [/SL:KB]
#include "llchatbar.h"
#include "llchatitemscontainerctrl.h"
#include "llfirstuse.h"
#include "llfloaterscriptdebug.h"
#include "llhints.h"
#include "llfloaterimnearbychat.h"
#include "llrecentpeople.h"

#include "llviewercontrol.h"

#include "llfloaterreg.h"//for LLFloaterReg::getTypedInstance
#include "llviewerwindow.h"//for screen channel position
#include "llfloaterimnearbychat.h"
#include "llfloaterimcontainer.h"
#include "llrootview.h"
#include "lllayoutstack.h"

// [RLVa:KB] - Checked: RLVa-2.0.0
#include "rlvactions.h"
#include "rlvcommon.h"
// [/RLVa:KB]

//add LLFloaterIMNearbyChatHandler to LLNotificationsUI namespace

using namespace LLNotificationsUI;

static LLFloaterIMNearbyChatToastPanel* createToastPanel()
{
    LLFloaterIMNearbyChatToastPanel* item = LLFloaterIMNearbyChatToastPanel::createInstance();
    return item;
}


//-----------------------------------------------------------------------------------------------
//LLFloaterIMNearbyChatScreenChannel
//-----------------------------------------------------------------------------------------------

class LLFloaterIMNearbyChatScreenChannel: public LLScreenChannelBase
{
    LOG_CLASS(LLFloaterIMNearbyChatScreenChannel);
public:
    typedef std::vector<LLHandle<LLToast> > toast_vec_t;
    typedef std::list<LLHandle<LLToast> > toast_list_t;

    LLFloaterIMNearbyChatScreenChannel(const Params& p)
        : LLScreenChannelBase(p)
    {
        mStopProcessing = false;

        LLControlVariable* ctrl = gSavedSettings.getControl("NearbyToastLifeTime").get();
        if (ctrl)
        {
            ctrl->getSignal()->connect(boost::bind(&LLFloaterIMNearbyChatScreenChannel::updateToastsLifetime, this));
        }

        ctrl = gSavedSettings.getControl("NearbyToastFadingTime").get();
        if (ctrl)
        {
            ctrl->getSignal()->connect(boost::bind(&LLFloaterIMNearbyChatScreenChannel::updateToastFadingTime, this));
        }

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-08-27 (Catznip-2.1)
        ctrl = gSavedSettings.getControl("NearbyToastWidth").get();
        if (ctrl)
        {
            // updateToastWidth() will call getToastWidth() which will set up/break down mChatBarReshapeConnection as needed
            ctrl->getSignal()->connect(boost::bind(&LLFloaterIMNearbyChatScreenChannel::updateToastWidth, this));
        }
// [/SL:KB]

        ctrl = gSavedSettings.getControl("AlchemyNearbyChatInput").get();
        if (ctrl)
        {
            ctrl->getSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { mChatBarReshapeConnection.disconnect(); });
        }
    }

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    ~LLFloaterIMNearbyChatScreenChannel()
    {
        mChatBarReshapeConnection.disconnect();
    }
// [/SL:KB]

    void addChat    (LLSD& chat);
    void arrangeToasts      ();

    typedef boost::function<LLFloaterIMNearbyChatToastPanel* (void )> create_toast_panel_callback_t;
    void setCreatePanelCallback(create_toast_panel_callback_t value) { m_create_toast_panel_callback_t = value;}

    void onToastDestroyed   (LLToast* toast, bool app_quitting);
    void onToastFade        (LLToast* toast);

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-08-27 (Catznip-2.1)
    S32  getToastWidth();
    void updateToastWidth();
// [/SL:KB]

    void redrawToasts()
    {
        arrangeToasts();
    }

    // hide all toasts from screen, but not remove them from a channel
    // removes all toasts from a channel
    virtual void        removeToastsFromChannel()
    {
        for(toast_vec_t::iterator it = m_active_toasts.begin(); it != m_active_toasts.end(); ++it)
        {
            addToToastPool(it->get());
        }
        m_active_toasts.clear();
    };

    virtual void deleteAllChildren()
    {
        LL_DEBUGS("NearbyChat") << "Clearing toast pool" << LL_ENDL;
        m_toast_pool.clear();
        m_active_toasts.clear();
        LLScreenChannelBase::deleteAllChildren();
    }

protected:
    void    deactivateToast(LLToast* toast);
    void    addToToastPool(LLToast* toast)
    {
        if (!toast) return;
        LL_DEBUGS("NearbyChat") << "Pooling toast" << LL_ENDL;
        toast->setVisible(FALSE);
        toast->stopTimer();
        toast->setIsHidden(true);

        // Nearby chat toasts that are hidden, not destroyed. They are collected to the toast pool, so that
        // they can be used next time, this is done for performance. But if the toast lifetime was changed
        // (from preferences floater (STORY-36)) while it was shown (at this moment toast isn't in the pool yet)
        // changes don't take affect.
        // So toast's lifetime should be updated each time it's added to the pool. Otherwise viewer would have
        // to be restarted so that changes take effect.
        toast->setLifetime(gSavedSettings.getS32("NearbyToastLifeTime"));
        toast->setFadingTime(gSavedSettings.getS32("NearbyToastFadingTime"));
        m_toast_pool.push_back(toast->getHandle());
    }

    void    createOverflowToast(S32 bottom, F32 timer);

    void    updateToastsLifetime();

    void    updateToastFadingTime();

    create_toast_panel_callback_t m_create_toast_panel_callback_t;

    bool    createPoolToast();

    toast_vec_t m_active_toasts;
    toast_list_t m_toast_pool;

    bool    mStopProcessing;
    bool    mChannelRect;

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    boost::signals2::connection mChatBarReshapeConnection;
// [/SL:KB]
};



//-----------------------------------------------------------------------------------------------
// LLFloaterIMNearbyChatToast
//-----------------------------------------------------------------------------------------------

// We're deriving from LLToast to be able to override onClose()
// in order to handle closing nearby chat toasts properly.
class LLFloaterIMNearbyChatToast final : public LLToast
{
    LOG_CLASS(LLFloaterIMNearbyChatToast);
public:
    LLFloaterIMNearbyChatToast(const LLToast::Params& p, LLFloaterIMNearbyChatScreenChannel* nc_channelp)
    :   LLToast(p),
        mNearbyChatScreenChannelp(nc_channelp)
    {
    }

    /*virtual*/ void onClose(bool app_quitting);

private:
    LLFloaterIMNearbyChatScreenChannel* mNearbyChatScreenChannelp;
};

//-----------------------------------------------------------------------------------------------
// LLFloaterIMNearbyChatScreenChannel
//-----------------------------------------------------------------------------------------------

void LLFloaterIMNearbyChatScreenChannel::deactivateToast(LLToast* toast)
{
    toast_vec_t::iterator pos = std::find(m_active_toasts.begin(), m_active_toasts.end(), toast->getHandle());

    if (pos != m_active_toasts.end())
    {
        LL_DEBUGS("NearbyChat") << "Deactivating toast" << LL_ENDL;
        m_active_toasts.erase(pos);
    }
}

void    LLFloaterIMNearbyChatScreenChannel::createOverflowToast(S32 bottom, F32 timer)
{
    //we don't need overflow toast in nearby chat
}

void LLFloaterIMNearbyChatScreenChannel::onToastDestroyed(LLToast* toast, bool app_quitting)
{
    LL_DEBUGS("NearbyChat") << "Toast destroyed (app_quitting=" << app_quitting << ")" << LL_ENDL;

    if (app_quitting)
    {
        // Viewer is quitting.
        // Immediately stop processing chat messages (EXT-1419).
        mStopProcessing = true;
    }
    else
    {
        // The toast is being closed by user (STORM-192).
        // Remove it from the list of active toasts to prevent
        // further references to the invalid pointer.
        deactivateToast(toast);
    }
}

void LLFloaterIMNearbyChatScreenChannel::onToastFade(LLToast* toast)
{
    LL_DEBUGS("NearbyChat") << "Toast fading" << LL_ENDL;

    //fade mean we put toast to toast pool
    if(!toast)
        return;

    deactivateToast(toast);

    addToToastPool(toast);

    arrangeToasts();
}

void LLFloaterIMNearbyChatScreenChannel::updateToastsLifetime()
{
    S32 seconds = gSavedSettings.getS32("NearbyToastLifeTime");
    toast_list_t::iterator it;

    for(it = m_toast_pool.begin(); it != m_toast_pool.end(); ++it)
    {
        (*it).get()->setLifetime(seconds);
    }
}

void LLFloaterIMNearbyChatScreenChannel::updateToastFadingTime()
{
    S32 seconds = gSavedSettings.getS32("NearbyToastFadingTime");
    toast_list_t::iterator it;

    for(it = m_toast_pool.begin(); it != m_toast_pool.end(); ++it)
    {
        (*it).get()->setFadingTime(seconds);
    }
}

bool    LLFloaterIMNearbyChatScreenChannel::createPoolToast()
{
    LLFloaterIMNearbyChatToastPanel* panel= m_create_toast_panel_callback_t();
    if(!panel)
        return false;

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-08-27 (Catznip-2.1)
    LLRect rctPanel = panel->getRect();
    rctPanel.setLeftTopAndSize(rctPanel.mLeft, rctPanel.mTop, getToastWidth(), rctPanel.getHeight());
    panel->setRect(rctPanel);
// [/SL:KB]

    LLToast::Params p;
    p.panel = panel;
    p.lifetime_secs = gSavedSettings.getS32("NearbyToastLifeTime");
    p.fading_time_secs = gSavedSettings.getS32("NearbyToastFadingTime");

    LLToast* toast = new LLFloaterIMNearbyChatToast(p, this);


    toast->setOnFadeCallback(boost::bind(&LLFloaterIMNearbyChatScreenChannel::onToastFade, this, _1));

    // If the toast gets somehow prematurely destroyed, deactivate it to prevent crash (STORM-1352).
    toast->setOnToastDestroyedCallback(boost::bind(&LLFloaterIMNearbyChatScreenChannel::onToastDestroyed, this, _1, false));

    LL_DEBUGS("NearbyChat") << "Creating and pooling toast" << LL_ENDL;
    m_toast_pool.push_back(toast->getHandle());
    return true;
}

void LLFloaterIMNearbyChatScreenChannel::addChat(LLSD& chat)
{
    //look in pool. if there is any message
    if(mStopProcessing)
        return;

    if (mFloaterSnapRegion == NULL)
    {
        mFloaterSnapRegion = gViewerWindow->getFloaterSnapRegion();
    }
    LLRect channel_rect;
    mFloaterSnapRegion->localRectToOtherView(mFloaterSnapRegion->getLocalRect(), &channel_rect, gFloaterView);
    chat["available_height"] = channel_rect.getHeight() - channel_rect.mBottom - ALControlCache::ToastGap - 110;;

    /*
    find last toast and check ID
    */

    if(m_active_toasts.size())
    {
        LLUUID fromID = chat["from_id"].asUUID();       // agent id or object id
        std::string from = chat["from"].asString();
        LLToast* toast = m_active_toasts[0].get();
        if (toast)
        {
            LLFloaterIMNearbyChatToastPanel* panel = dynamic_cast<LLFloaterIMNearbyChatToastPanel*>(toast->getPanel());

            if(panel && panel->messageID() == fromID && panel->getFromName() == from && panel->canAddText())
            {
                panel->addMessage(chat);
                toast->reshapeToPanel();
                toast->startTimer();

                arrangeToasts();
                return;
            }
        }
    }



    if(m_toast_pool.empty())
    {
        //"pool" is empty - create one more panel
        LL_DEBUGS("NearbyChat") << "Empty pool" << LL_ENDL;
        if(!createPoolToast())//created toast will go to pool. so next call will find it
            return;
        addChat(chat);
        return;
    }

    int chat_type = chat["chat_type"].asInteger();

    if( ((EChatType)chat_type == CHAT_TYPE_DEBUG_MSG))
    {
        if(gSavedSettings.getBOOL("ShowScriptErrors") == FALSE)
            return;
        if(gSavedSettings.getS32("ShowScriptErrorsLocation")== 1)
            return;
    }


    //take 1st element from pool, (re)initialize it, put it in active toasts

    LL_DEBUGS("NearbyChat") << "Getting toast from pool" << LL_ENDL;
    LLToast* toast = m_toast_pool.back().get();

    m_toast_pool.pop_back();


    LLFloaterIMNearbyChatToastPanel* panel = dynamic_cast<LLFloaterIMNearbyChatToastPanel*>(toast->getPanel());
    if(!panel)
        return;
    panel->init(chat);

    toast->reshapeToPanel();
    toast->startTimer();

    m_active_toasts.push_back(toast->getHandle());

    arrangeToasts();
}

static bool sort_toasts_predicate(LLHandle<LLToast> first, LLHandle<LLToast> second)
{
    if (!first.get() || !second.get()) return false; // STORM-1352

    F32 v1 = first.get()->getTimeLeftToLive();
    F32 v2 = second.get()->getTimeLeftToLive();
    return v1 > v2;
}

void LLFloaterIMNearbyChatScreenChannel::arrangeToasts()
{
// [SL:KB] - Patch: Chat-NearbyToastHeightRatio | Checked: Catznip-5.3
    const float nToastHeightRatio = llclamp(gSavedSettings.getS32("NearbyToastHeightRatio"), 30, 100) / 100.f;
// [/SL:KB]

    if(mStopProcessing || isHovering())
        return;

    if (mFloaterSnapRegion == NULL)
    {
        mFloaterSnapRegion = gViewerWindow->getFloaterSnapRegion();
    }

    if (!getParent())
    {
        // connect to floater snap region just to get resize events, we don't care about being a proper widget
        mFloaterSnapRegion->addChild(this);
        setFollows(FOLLOWS_ALL);
    }

    LLRect  toast_rect;
    updateRect();

    LLRect channel_rect;
    mFloaterSnapRegion->localRectToOtherView(mFloaterSnapRegion->getLocalRect(), &channel_rect, gFloaterView);
    channel_rect.mLeft += 10;
    channel_rect.mRight = channel_rect.mLeft + 300;
// [SL:KB] - Patch: Chat-NearbyToastHeightRatio | Checked: Catznip-5.3
    channel_rect.mTop = channel_rect.mBottom + channel_rect.getHeight() * nToastHeightRatio;
// [/SL:KB]

    S32 channel_bottom = channel_rect.mBottom;

//  S32     bottom = channel_bottom + 80;
// [SL:KB] - Patch: Chat-NearbyChatBar | Checked: 2012-01-17 (Catznip-3.2)
    S32     bottom = channel_bottom + gSavedSettings.getS32("NearbyToastOffset");
// [/SL:KB]
    S32     margin = ALControlCache::ToastGap;

    //sort active toasts
    std::sort(m_active_toasts.begin(),m_active_toasts.end(),sort_toasts_predicate);

    //calc max visible item and hide other toasts.

    for(toast_vec_t::iterator it = m_active_toasts.begin(); it != m_active_toasts.end(); ++it)
    {
        LLToast* toast = it->get();
        if (!toast)
        {
            LL_WARNS() << "NULL found in the active chat toasts list!" << LL_ENDL;
            continue;
        }

        S32 toast_top = bottom + toast->getRect().getHeight() + margin;

//      if(toast_top > channel_rect.getHeight())
// [SL:KB] - Patch: Chat-NearbyToastHeightRatio | Checked: Catznip-5.3
        // Make some allowances:
        //  * if a large toast appears (that currently won't fit the reserved height) then don't kill it and all other toasts
        //  * if only 2 toasts are visible only kill them if we're covering at least half the screen or if they're really too tall
        if ( (toast_top > channel_rect.mTop) && (it != m_active_toasts.begin()) &&
             ((m_active_toasts.size() > 3) || (nToastHeightRatio >= 0.5) || (toast_top > channel_rect.mBottom + channel_rect.getHeight() * 1.5)) )
// [/SL:KB]
        {
            while(it!=m_active_toasts.end())
            {
                addToToastPool(it->get());
                it=m_active_toasts.erase(it);
            }
            break;
        }

        toast_rect = toast->getRect();
        toast_rect.setLeftTopAndSize(channel_rect.mLeft , bottom + toast_rect.getHeight(), toast_rect.getWidth() ,toast_rect.getHeight());

        toast->setRect(toast_rect);
        bottom += toast_rect.getHeight() - toast->getTopPad() + margin;
    }

    // use reverse order to provide correct z-order and avoid toast blinking

    for(toast_vec_t::reverse_iterator it = m_active_toasts.rbegin(); it != m_active_toasts.rend(); ++it)
    {
        LLToast* toast = it->get();
        if (toast)
    {
        toast->setIsHidden(false);
        toast->setVisible(TRUE);
        }
    }

}

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
S32 LLFloaterIMNearbyChatScreenChannel::getToastWidth()
{
    static LLCachedControl<S32> s_nToastWidth(gSavedSettings, "NearbyToastWidth", 0);
    if (0 == s_nToastWidth)                 // Follow the width of the nearby chat bar
    {
        static S32 s_nLastToastWidth = 400;
        static LLCachedControl<bool> sUseChatbar(gSavedSettings, "AlchemyNearbyChatInput", true);
        if (!sUseChatbar)
        {
            LLFloaterIMNearbyChat* pNearbyChat = LLFloaterReg::findTypedInstance<LLFloaterIMNearbyChat>("nearby_chat");
            if (pNearbyChat)
            {
                if (!mChatBarReshapeConnection.connected())
                {
                    mChatBarReshapeConnection = pNearbyChat->setReshapeCallback(boost::bind(&LLFloaterIMNearbyChatScreenChannel::updateToastWidth, this));
                }

                // We're using the width of the nearby chat floater since it'll be nearly the width of the nearby chat bar anyway
                if ((!pNearbyChat->isMinimized()) && ((pNearbyChat->isTornOff()) || (!pNearbyChat->getHost()) || (!pNearbyChat->getHost()->isMinimized())))
                {
                    s_nLastToastWidth = llmax(pNearbyChat->getRect().getWidth(), 400);
                }
            }
        }
        else
        {
            LLChatBar* pNearbyChat = LLFloaterReg::getTypedInstance<LLChatBar>("chatbar");
            if (pNearbyChat)
            {
                if (!mChatBarReshapeConnection.connected())
                {
                    mChatBarReshapeConnection = pNearbyChat->setReshapeCallback(boost::bind(&LLFloaterIMNearbyChatScreenChannel::updateToastWidth, this));
                }

                // We're using the width of the chatbar floater
                //if ((!pNearbyChat->isMinimized()) && ((pNearbyChat->isTornOff()) || (!pNearbyChat->getHost()) || (!pNearbyChat->getHost()->isMinimized())))
                {
                    s_nLastToastWidth = llmax(pNearbyChat->getRect().getWidth(), 400);
                }
            }
        }
        return s_nLastToastWidth;
    }

    if (mChatBarReshapeConnection.connected())
    {
        mChatBarReshapeConnection.disconnect();
    }

    return llmax((S32)s_nToastWidth, 400);  // Provide a sane lower threshold for toast width
}

void LLFloaterIMNearbyChatScreenChannel::updateToastWidth()
{
    static S32 s_nToastWidthPrev = 0;

    // Do nothing if the toast width hasn't actually changed
    S32 nToastWidth = getToastWidth();
    if (s_nToastWidthPrev == nToastWidth)
        return;
    s_nToastWidthPrev = nToastWidth;

    //
    // Resize the active toasts
    //
    for(toast_vec_t::iterator itActive = m_active_toasts.begin(); itActive != m_active_toasts.end(); ++itActive)
    {
        LLToast* pToast = (*itActive).get();
        LLFloaterIMNearbyChatToastPanel* pToastPanel = (pToast) ? dynamic_cast<LLFloaterIMNearbyChatToastPanel*>(pToast->getPanel()) : NULL;
        if (!pToastPanel)
            continue;

        LLRect rctToastPanel = pToastPanel->getRect();
        rctToastPanel.setLeftTopAndSize(rctToastPanel.mLeft, rctToastPanel.mTop, nToastWidth, rctToastPanel.getHeight());
        pToastPanel->reshape(rctToastPanel.getWidth(), rctToastPanel.getHeight(), 1);
        pToastPanel->setRect(rctToastPanel);
        pToastPanel->snapToMessageHeight();
        pToast->reshapeToPanel();
    }
    arrangeToasts();

    //
    // Resize toasts in the toast pool
    //
    for(toast_list_t::iterator itPool = m_toast_pool.begin(); itPool != m_toast_pool.end(); ++itPool)
    {
        LLToast* pToast = (*itPool).get();
        LLFloaterIMNearbyChatToastPanel* pToastPanel = (pToast) ? dynamic_cast<LLFloaterIMNearbyChatToastPanel*>(pToast->getPanel()) : NULL;
        if (!pToastPanel)
            continue;

        LLRect rctToastPanel = pToastPanel->getRect();
        rctToastPanel.setLeftTopAndSize(rctToastPanel.mLeft, rctToastPanel.mTop, nToastWidth, rctToastPanel.getHeight());
        pToastPanel->setRect(rctToastPanel);
    }
}
// [/SL:KB]

//-----------------------------------------------------------------------------------------------
//LLFloaterIMNearbyChatHandler
//-----------------------------------------------------------------------------------------------
std::unique_ptr<LLEventPump> LLFloaterIMNearbyChatHandler::sChatWatcher(new LLEventStream("LLChat"));

LLFloaterIMNearbyChatHandler::LLFloaterIMNearbyChatHandler()
{
    // Getting a Channel for our notifications
    LLFloaterIMNearbyChatScreenChannel::Params p;
    p.id = NEARBY_CHAT_CHANNEL_UUID;
    LLFloaterIMNearbyChatScreenChannel* channel = new LLFloaterIMNearbyChatScreenChannel(p);

    LLFloaterIMNearbyChatScreenChannel::create_toast_panel_callback_t callback = createToastPanel;

    channel->setCreatePanelCallback(callback);

    LLChannelManager::getInstance()->addChannel(channel);

    mChannel = channel->getHandle();
}

LLFloaterIMNearbyChatHandler::~LLFloaterIMNearbyChatHandler()
{
}


void LLFloaterIMNearbyChatHandler::initChannel()
{
    //LLRect snap_rect = gFloaterView->getSnapRect();
    //mChannel->init(snap_rect.mLeft, snap_rect.mLeft + 200);
}



void LLFloaterIMNearbyChatHandler::processChat(const LLChat& chat_msg,
                                      const LLSD &args)
{
    if(chat_msg.mMuted == TRUE)
        return;

    if(chat_msg.mText.empty())
        return;//don't process empty messages

// [RLVa:KB] - Checked: 2010-04-20 (RLVa-1.2.0f) | Modified: RLVa-1.2.0f
    if (RlvActions::isRlvEnabled())
    {
        // NOTE-RLVa: we can only filter the *message* here since most everything else will already be part of "args" as well
        LLChat& tmp_chat = const_cast<LLChat&>(chat_msg);
        if ( (!RlvActions::canShowLocation()) && (!tmp_chat.mRlvLocFiltered) && (CHAT_SOURCE_AGENT != tmp_chat.mSourceType) )
        {
            RlvUtil::filterLocation(tmp_chat.mText);
            tmp_chat.mRlvLocFiltered = TRUE;
        }
        if ( (!RlvActions::canShowName(RlvActions::SNC_DEFAULT)) && (!tmp_chat.mRlvNamesFiltered) && (CHAT_SOURCE_AGENT != tmp_chat.mSourceType) )
        {
            RlvUtil::filterNames(tmp_chat.mText);
            tmp_chat.mRlvNamesFiltered = TRUE;
        }
    }
// [/RLVa:KB]

    LLFloaterReg::getInstance("im_container");
    LLFloaterIMNearbyChat* nearby_chat = LLFloaterReg::getTypedInstance<LLFloaterIMNearbyChat>("nearby_chat");

    // Build notification data
    LLSD chat;
    chat["message"] = chat_msg.mText;
    chat["from"] = chat_msg.mFromName;
    chat["from_id"] = chat_msg.mFromID;
    chat["time"] = chat_msg.mTime;
    chat["source"] = (S32)chat_msg.mSourceType;
    chat["chat_type"] = (S32)chat_msg.mChatType;
    chat["chat_style"] = (S32)chat_msg.mChatStyle;
    // Pass sender info so that it can be rendered properly (STORM-1021).
//  chat["sender_slurl"] = LLViewerChat::getSenderSLURL(chat_msg, args);
// [RLVa:KB] - Checked: 2011-12-13 (RLVa-1.4.6) | Added: RLVa-1.4.6
    if ((CHAT_SOURCE_AGENT != chat_msg.mSourceType) || (!chat_msg.mRlvNamesFiltered))
        chat["sender_slurl"] = LLViewerChat::getSenderSLURL(chat_msg, args);
// [/RLVa:KB]

    if (chat_msg.mChatType == CHAT_TYPE_DIRECT &&
        chat_msg.mText.length() > 0 &&
        chat_msg.mText[0] == '@')
    {
        // Send event on to LLEventStream and exit
        sChatWatcher->post(chat);
        return;
    }

    // don't show toast and add message to chat history on receive debug message
    // with disabled setting showing script errors or enabled setting to show script
    // errors in separate window.
    if (chat_msg.mChatType == CHAT_TYPE_DEBUG_MSG)
    {
        if (LLFloater::isQuitRequested()) return;

        if(gSavedSettings.getBOOL("ShowScriptErrors") == FALSE)
            return;

        // don't process debug messages from not owned objects, see EXT-7762
        if (gAgentID != chat_msg.mOwnerID)
        {
            return;
        }

        if (gSavedSettings.getS32("ShowScriptErrorsLocation")== 1)// show error in window //("ScriptErrorsAsChat"))
        {

            LLColor4 txt_color;

            LLViewerChat::getChatColor(chat_msg,txt_color);

            LLFloaterScriptDebug::addScriptLine(chat_msg.mText,
                                                chat_msg.mFromName,
                                                txt_color,
                                                chat_msg.mFromID);
            return;
        }
    }

    nearby_chat->addMessage(chat_msg, true, args);

    if(chat_msg.mSourceType == CHAT_SOURCE_AGENT
        && chat_msg.mFromID.notNull()
        && chat_msg.mFromID != gAgentID)
    {
        LLFirstUse::otherAvatarChatFirst();

        // Add sender to the recent people list.
// [RLVa:KB] - Checked: RLVa-2.0.0
        if ( (!RlvActions::isRlvEnabled()) || (RlvActions::canShowName(RlvActions::SNC_DEFAULT, chat_msg.mFromID)) )
        {
            LLSD userdata;
            userdata["date"] = LLDate::now();
            userdata["nearby"] = true;
            LLRecentPeople::instance().add(chat_msg.mFromID, userdata);
    }
// [/RLVa:KB]
    }

    // Send event on to LLEventStream
    sChatWatcher->post(chat);

    if((  ( chat_msg.mSourceType == CHAT_SOURCE_AGENT
            && gSavedSettings.getBOOL("UseChatBubbles") )
        || mChannel.isDead()
        || !mChannel.get()->getShowToasts() )
        || nearby_chat->isMessagePanelVisible())
        // <RYE>
        // no need to toast if bubble chat is enabled or nearby chat toasts are disabled
        // or if in Do Not Disturb mode
        // or if conversation is visible and selected and not collapsed
        return;

    // arrange a channel on a screen
    if(!mChannel.get()->getVisible())
    {
        initChannel();
    }

    /*
    //comment all this due to EXT-4432
    ..may clean up after some time...

    //only messages from AGENTS
    if(CHAT_SOURCE_OBJECT == chat_msg.mSourceType)
    {
        if(chat_msg.mChatType == CHAT_TYPE_DEBUG_MSG)
            return;//ok for now we don't skip messeges from object, so skip only debug messages
    }
    */

    LLFloaterIMNearbyChatScreenChannel* channel = dynamic_cast<LLFloaterIMNearbyChatScreenChannel*>(mChannel.get());

    if(channel)
    {
        // Handle IRC styled messages.
        std::string toast_msg;
        if (chat_msg.mChatStyle == CHAT_STYLE_IRC)
        {
            if (!chat_msg.mFromName.empty())
            {
                toast_msg += chat_msg.mFromName;
            }
            toast_msg += chat_msg.mText.substr(3);
        }
        else
        {
            toast_msg = chat_msg.mText;
        }

        // This is insane.
        //bool chat_overlaps = false;
        //if(nearby_chat->getChatHistory())
        //{
        //  LLRect chat_rect = nearby_chat->getChatHistory()->calcScreenRect();
        //  for (std::list<LLView*>::const_iterator child_iter = gFloaterView->getChildList()->begin();
        //       child_iter != gFloaterView->getChildList()->end(); ++child_iter)
        //  {
        //      LLView *view = *child_iter;
        //      const LLRect& rect = view->getRect();
        //      if(view->isInVisibleChain() && (rect.overlaps(chat_rect)))
        //      {
        //          if(!nearby_chat->getChatHistory()->hasAncestor(view))
        //          {
        //              chat_overlaps = true;
        //          }
        //          break;
        //      }
        //  }
        //}
        ////Don't show nearby toast, if conversation is visible and selected
        //if ((nearby_chat->hasFocus()) ||
        //  (LLFloater::isVisible(nearby_chat) && nearby_chat->isTornOff() && !nearby_chat->isMinimized()) ||
        //    ((im_box->getSelectedSession().isNull() && !chat_overlaps &&
        //      ((LLFloater::isVisible(im_box) && !nearby_chat->isTornOff() && !im_box->isMinimized())
        //              || (LLFloater::isVisible(nearby_chat) && nearby_chat->isTornOff() && !nearby_chat->isMinimized())))))
        //{
        //  if(nearby_chat->isMessagePaneExpanded())
        //  {
        //      return;
        //  }
        //}

        //Will show toast when chat preference is set
        if((gSavedSettings.getString("NotificationNearbyChatOptions") == "toast"))
        {
            // Add a nearby chat toast.
            LLUUID id;
            id.generate();
            chat["id"] = id;
// [RLVa:KB] - Checked: RLVa-1.2.0
            if (RlvActions::isRlvEnabled())
                chat["show_icon_tooltip"] = !chat_msg.mRlvNamesFiltered;
// [/RLVa:KB]
            std::string r_color_name = "White";
            F32 r_color_alpha = 1.0f;
            LLViewerChat::getChatColor( chat_msg, r_color_name, r_color_alpha);

            chat["text_color"] = r_color_name;
            chat["color_alpha"] = r_color_alpha;
            chat["font_size"] = (S32)LLViewerChat::getChatFontSize() ;
            chat["message"] = toast_msg;
            channel->addChat(chat);
        }

    }
}


//-----------------------------------------------------------------------------------------------
// LLFloaterIMNearbyChatToast
//-----------------------------------------------------------------------------------------------

// virtual
void LLFloaterIMNearbyChatToast::onClose(bool app_quitting)
{
    mNearbyChatScreenChannelp->onToastDestroyed(this, app_quitting);
}

// EOF
