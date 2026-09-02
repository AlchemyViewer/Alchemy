/**
 * @file alchatbar.cpp
 * @brief ALChatBar class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * Alchemy Viewer Source Code
 * Copyright © 2026, Rye <rye@alchemyviewer.org>
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

#include "alchatbar.h"

#include "llautoreplace.h"
#include "llfontgl.h"
#include "llrect.h"
#include "llerror.h"
#include "llparcel.h"
#include "llstring.h"
#include "message.h"
#include "llfocusmgr.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "lltrans.h"
#include "llchatentry.h"

#include "alchatautocomplete.h"
#include "alchatcommand.h"
#include "llagent.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "llcommandhandler.h"   // secondlife:///app/chat/ support
#include "llviewercontrol.h"
#include "llgesturemgr.h"
#include "llfloaterimnearbychat.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llstatusbar.h"
#include "lltextbox.h"
#include "lluiconstants.h"
#include "llviewergesture.h"            // for triggering gestures
#include "llviewermenu.h"       // for deleting object with DEL key
#include "llviewerstats.h"
#include "llviewerwindow.h"
#include "llframetimer.h"
#include "llresmgr.h"
#include "llworld.h"
#include "llinventorymodel.h"
#include "llmultigesture.h"
#include "llui.h"
#include "lluictrlfactory.h"
#include "llviewerchat.h"
// [RLVa:KB] - Checked: 2010-02-27 (RLVa-1.2.0b)
#include "rlvactions.h"
#include "rlvcommon.h"
// [/RLVa:KB]

class ALChatBarGestureObserver final : public LLGestureManagerObserver
{
public:
    ALChatBarGestureObserver(ALChatBar* chat_barp) : mChatBar(chat_barp){}
    virtual ~ALChatBarGestureObserver() = default;
    void changed() override { mChatBar->refreshGestures(); }
private:
    ALChatBar* mChatBar;
};


//extern void send_chat_from_viewer(const std::string& utf8_out_text, EChatType type, S32 channel);
// [RLVa:KB] - Checked: 2010-02-27 (RLVa-0.2.2)
extern void send_chat_from_viewer(std::string utf8_out_text, EChatType type, S32 channel);
// [/RLVa:KB]

//
// Functions
//

ALChatBar::ALChatBar(const LLSD& key)
:   LLFloater(key),
    mInputEditor(nullptr),
    mGestureLabelTimer(),
    mIsBuilt(FALSE),
    mGestureCombo(nullptr),
    mObserver(nullptr)
{
    mCommitCallbackRegistrar.add("Chatbar.Shout", boost::bind(&ALChatBar::sendChat, this, CHAT_TYPE_SHOUT));
    mCommitCallbackRegistrar.add("Chatbar.Whisper", boost::bind(&ALChatBar::sendChat, this, CHAT_TYPE_WHISPER));
}


ALChatBar::~ALChatBar()
{
    LLGestureMgr::instance().removeObserver(mObserver);
    delete mObserver;
    mObserver = nullptr;

    delete mReshapeSignal;
    mReshapeSignal = nullptr;

    mChatChannelConnection.disconnect();
    mChatFontSizeConnection.disconnect();
}

//-----------------------------------------------------------------------
// Overrides
//-----------------------------------------------------------------------

bool ALChatBar::postBuild()
{
    // * NOTE: mantipov: getChild with default parameters returns dummy widget.
    // Seems this class will be completle removed
    // attempt to bind to an existing combo box named gesture
    setGestureCombo(findChild<LLComboBox>("Gesture"));

    mInputEditor = getChild<LLChatEntry>("Chat Editor");
    mInputEditor->setAutoreplaceCallback(boost::bind(&LLAutoReplace::autoreplaceCallback, LLAutoReplace::getInstance(), _1, _2, _3, _4, _5));
    mInputEditor->setKeystrokeCallback(boost::bind(&ALChatBar::onInputEditorKeystroke, this, _1));
    mInputEditor->setFocusLostCallback(boost::bind(&ALChatBar::onInputEditorFocusLost));
    mInputEditor->setFocusReceivedCallback(boost::bind(&ALChatBar::onInputEditorGainFocus));
    mInputEditor->setCommitOnFocusLost( false );
    mInputEditor->setPassDelete(true);
    mInputEditor->setShowChatMentionPicker(true);
    mInputEditor->setShowEmojiHelper(true);
    mInputEditor->enableSingleLineMode(true);
    changeChannelLabel(gSavedSettings.getS32("AlchemyNearbyChatChannel"));
    mInputEditor->setMaxTextLength(DB_CHAT_MSG_STR_LEN * 5);

    mInputEditor->setFont(LLViewerChat::getChatFont());

    mChatChannelConnection = gSavedSettings.getControl("AlchemyNearbyChatChannel")->getCommitSignal()->connect([this](LLControlVariable*, const LLSD& newval, const LLSD&) { changeChannelLabel(newval.asInteger()); });
    mChatFontSizeConnection = gSavedSettings.getControl("ChatFontSize")->getSignal()->connect([this](LLControlVariable* control, const LLSD& new_val, const LLSD& old_val) { mInputEditor->setFont(LLViewerChat::getChatFont()); });

    return TRUE;
}

void ALChatBar::onOpen(const LLSD& key)
{
    mInputEditor->setFocus(TRUE);
}

// virtual
bool ALChatBar::handleKeyHere( KEY key, MASK mask )
{
    bool handled = FALSE;

    if( KEY_RETURN == key )
    {
        if (mask == (MASK_CONTROL | MASK_SHIFT))
        {
            // whisper
            sendChat(CHAT_TYPE_WHISPER);
            handled = TRUE;
        }
        else if (mask == MASK_CONTROL)
        {
            // shout
            sendChat(CHAT_TYPE_SHOUT);
            handled = TRUE;
        }
        else if (mask == MASK_ALT)
        {
            // shout
            sendChat(CHAT_TYPE_OOC);
            handled = TRUE;
        }
        else if (mask == MASK_NONE)
        {
            // say
            sendChat( CHAT_TYPE_NORMAL );
            handled = TRUE;
        }
    }
    // only do this in main chatbar
    else if ( KEY_ESCAPE == key)
    {
        stopChat();

        handled = TRUE;
    }

    return handled;
}

void ALChatBar::onFocusLost()
{
    stopChat();
}

void ALChatBar::refresh()
{
    // HACK: Leave the name of the gesture in place for a few seconds.
    const F32 SHOW_GESTURE_NAME_TIME = 2.f;
    if (mGestureLabelTimer.getStarted() && mGestureLabelTimer.getElapsedTimeF32() > SHOW_GESTURE_NAME_TIME)
    {
        LLCtrlListInterface* gestures = mGestureCombo ? mGestureCombo->getListInterface() : NULL;
        if (gestures) gestures->selectFirstItem();
        mGestureLabelTimer.stop();
    }

    if ((gAgent.getTypingTime() > LLAgent::TYPING_TIMEOUT_SECS) && (gAgent.getRenderState() & AGENT_STATE_TYPING))
    {
        gAgent.stopTyping();
    }
}

void ALChatBar::refreshGestures()
{
    if (mGestureCombo)
    {
        //store current selection so we can maintain it
        std::string cur_gesture = mGestureCombo->getValue().asString();
        mGestureCombo->selectFirstItem();

        // clear
        mGestureCombo->clearRows();

        // collect list of unique gestures
        std::map <std::string, bool> unique;
        LLGestureMgr::item_map_t::const_iterator it;
        const LLGestureMgr::item_map_t& active_gestures = LLGestureMgr::instance().getActiveGestures();
        for (it = active_gestures.begin(); it != active_gestures.end(); ++it)
        {
            LLMultiGesture* gesture = (*it).second;
            if (gesture)
            {
                if (!gesture->mTrigger.empty())
                {
                    unique[gesture->mTrigger] = TRUE;
                }
            }
        }

        // add unique gestures
        std::map <std::string, bool>::iterator it2;
        for (it2 = unique.begin(); it2 != unique.end(); ++it2)
        {
            mGestureCombo->addSimpleElement((*it2).first);
        }

        mGestureCombo->sortByName();
        // Insert label after sorting, at top, with separator below it
        mGestureCombo->addSeparator(ADD_TOP);
        mGestureCombo->addSimpleElement(getString("gesture_label"), ADD_TOP);

        if (!cur_gesture.empty())
        {
            mGestureCombo->selectByValue(LLSD(cur_gesture));
        }
        else
        {
            mGestureCombo->selectFirstItem();
        }
    }
}

// Move the cursor to the correct input field.
void ALChatBar::setKeyboardFocus(bool focus)
{
    if (focus)
    {
        if (mInputEditor)
        {
            mInputEditor->setFocus(TRUE);
            mInputEditor->selectAll();
        }
    }
    else if (gFocusMgr.childHasKeyboardFocus(this))
    {
        if (mInputEditor)
        {
            mInputEditor->deselect();
        }
        setFocus(FALSE);
    }
}

bool ALChatBar::inputEditorHasFocus() const
{
    return mInputEditor && mInputEditor->hasFocus();
}

std::string ALChatBar::getCurrentChat() const
{
    return mInputEditor ? mInputEditor->getText() : LLStringUtil::null;
}

void ALChatBar::setGestureCombo(LLComboBox* combo)
{
    mGestureCombo = combo;
    if (mGestureCombo)
    {
        mGestureCombo->setCommitCallback(boost::bind(&ALChatBar::onCommitGesture, this, _1));

        // now register observer since we have a place to put the results
        mObserver = new ALChatBarGestureObserver(this);
        LLGestureMgr::instance().addObserver(mObserver);

        // refresh list from current active gestures
        refreshGestures();
    }
}

//-----------------------------------------------------------------------
// Internal functions
//-----------------------------------------------------------------------

void ALChatBar::sendChat( EChatType type )
{
    if (mInputEditor)
    {
        LLWString text = mInputEditor->getConvertedText();
        LLWStringUtil::trim(text);
        LLWStringUtil::replaceChar(text, 182, '\n'); // Convert paragraph symbols back into newlines.
        if (!text.empty())
        {
            // Check if this is destined for another channel
            S32 channel = 0;
            LLFloaterIMNearbyChat::stripChannelNumber(text, &channel);

            LLFloaterIMNearbyChat* nearby_chat = LLFloaterReg::findTypedInstance<LLFloaterIMNearbyChat>("nearby_chat");
            if (nearby_chat)
            {
                nearby_chat->updateUsedEmojis(text);
            }

            std::string utf8text = wstring_to_utf8str(text);

            if (type == CHAT_TYPE_OOC)
            {
                utf8text = fmt::format("{} {} {}",
                                       gSavedSettings.getString("ChatOOCPrefix"),
                                       utf8text,
                                       gSavedSettings.getString("ChatOOCPostfix"));
            }

            // Try to trigger a gesture, if not chat to a script.
            std::string utf8_revised_text;
            if (0 == channel)
            {
                LLFloaterIMNearbyChat::applyOOCClose(utf8text);
                LLFloaterIMNearbyChat::applyMUPose(utf8text);

                // discard returned "found" boolean
                if (!LLGestureMgr::instance().triggerAndReviseString(utf8text, &utf8_revised_text))
                {
                    utf8_revised_text = utf8text;
                }
            }
            else
            {
                utf8_revised_text = utf8text;
            }

            utf8_revised_text = utf8str_trim(utf8_revised_text);

            EChatType nType = (type == CHAT_TYPE_OOC ? CHAT_TYPE_NORMAL : type);
            type            = LLFloaterIMNearbyChat::processChatTypeTriggers(nType, utf8_revised_text);

            if (!utf8_revised_text.empty() && !ALChatCommand::parseCommand(utf8_revised_text))
            {
                // Chat with animation
                LLFloaterIMNearbyChat::sendChatFromViewer(utf8_revised_text, type, gSavedSettings.getBOOL("PlayChatAnim"));
            }
        }

        mInputEditor->updateHistory();
        mInputEditor->setText(LLStringExplicit(""));
    }

    gAgent.stopTyping();

    // If the user wants to stop chatting on hitting return, lose focus
    // and go out of chat mode.
    if (gSavedSettings.getBOOL("CloseChatBarOnReturn"))
    {
        stopChat();
    }
}

void ALChatBar::changeChannelLabel(S32 channel)
{
    if (channel == 0)
        mInputEditor->setLabel(LLTrans::getString("NearbyChatTitle"));
    else
    {
        LLStringUtil::format_map_t args;
        args["CHANNEL"] = llformat("%d", channel);
        mInputEditor->setLabel(LLTrans::getString("NearbyChatTitleChannel", args));
    }
}

//-----------------------------------------------------------------------
// Static functions
//-----------------------------------------------------------------------

// static
void ALChatBar::startChat(const char* line)
{
    ALChatBar* bar = LLFloaterReg::getTypedInstance<ALChatBar>("chatbar");
    bar->setVisible(TRUE);
    bar->setFocus(TRUE);
    bar->mInputEditor->setFocus(TRUE);

    if (line)
    {
        std::string line_string(line);
        bar->mInputEditor->setText(line_string);
    }
    // always move cursor to end so users don't obliterate chat when accidentally hitting WASD
    bar->mInputEditor->endOfDoc();
}


// Exit "chat mode" and do the appropriate focus changes
// static
void ALChatBar::stopChat()
{
    ALChatBar* bar = LLFloaterReg::getTypedInstance<ALChatBar>("chatbar");
    bar->mInputEditor->setFocus(FALSE);
    bar->setVisible(FALSE);
    gAgent.stopTyping();
}

// static
void ALChatBar::updateChatFont()
{
    ALChatBar* bar = LLFloaterReg::getTypedInstance<ALChatBar>("chatbar");
    if (bar)
    {
        bar->mInputEditor->setFont(LLViewerChat::getChatFont());
    }
}

void ALChatBar::onInputEditorKeystroke(LLTextEditor* caller)
{
    LLWString raw_text;
    if (mInputEditor) raw_text = mInputEditor->getWText();

    // Can't trim the end, because that will cause autocompletion
    // to eat trailing spaces that might be part of a gesture.
    LLWStringUtil::trimHead(raw_text);

    S32 length = narrow(raw_text.length());

    if( (length > 0)
        && (raw_text[0] != '/')     // forward slash is used for escape (eg. emote) sequences
            && (raw_text[0] != ':') // colon is used in for MUD poses
      )
    {
        gAgent.startTyping();
    }
    else
    {
        gAgent.stopTyping();
    }

    KEY key = gKeyboard->currentKey();

    ALChatAutocomplete::update(
        mInputEditor,
        wstring_to_utf8str(raw_text),
        key,
        [this](const LLGestureAutocompleteHelper::Row& row, ALChatAutocomplete::CommitAction action)
        {
            if (action == ALChatAutocomplete::CommitAction::SUBMIT)
            {
                sendChat(CHAT_TYPE_NORMAL);
                return;
            }

            mInputEditor->setText(row.value + " ");
            mInputEditor->endOfDoc();
        });
}

// static
void ALChatBar::onInputEditorFocusLost()
{
    // stop typing animation
    gAgent.stopTyping();
}

// static
void ALChatBar::onInputEditorGainFocus()
{

}

void ALChatBar::onCommitGesture(LLUICtrl* ctrl)
{
    LLCtrlListInterface* gestures = mGestureCombo ? mGestureCombo->getListInterface() : NULL;
    if (gestures)
    {
        S32 index = gestures->getFirstSelectedIndex();
        if (index == 0)
        {
            return;
        }
        const std::string& trigger = gestures->getSelectedValue().asString();

        // pretend the user chatted the trigger string, to invoke
        // substitution and logging.
        std::string text(trigger);
        std::string revised_text;
        LLGestureMgr::instance().triggerAndReviseString(text, &revised_text);

        revised_text = utf8str_trim(revised_text);
        if (!revised_text.empty() && !ALChatCommand::parseCommand(revised_text))
        {
            // Don't play nodding animation
            LLFloaterIMNearbyChat::sendChatFromViewer(revised_text, CHAT_TYPE_NORMAL, FALSE);
        }
    }
    mGestureLabelTimer.start();
    if (mGestureCombo != nullptr)
    {
        // free focus back to chat bar
        mGestureCombo->setFocus(FALSE);
    }
}

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
// virtual
void ALChatBar::reshape(S32 width, S32 height, bool called_from_parent)
{
    LLFloater::reshape(width, height, called_from_parent);

    if (mReshapeSignal)
    {
        (*mReshapeSignal)(this, width, height);
    }
}

boost::signals2::connection ALChatBar::setReshapeCallback(const reshape_signal_t::slot_type& cb)
{
    if (!mReshapeSignal)
    {
        mReshapeSignal = new reshape_signal_t();
    }
    return mReshapeSignal->connect(cb);
}
// [/SL:KB]
