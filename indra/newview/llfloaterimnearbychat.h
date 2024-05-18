/**
 * @file llfloaterimnearbychat.h
 * @brief LLFloaterIMNearbyChat class definition
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (C) 2010-2016, Kitty Barnett
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

#ifndef LL_LLFLOATERIMNEARBYCHAT_H
#define LL_LLFLOATERIMNEARBYCHAT_H

#include "alchatcommand.h"
#include "llagent.h"
#include "llfloaterimsessiontab.h"
#include "llcombobox.h"
#include "llgesturemgr.h"
#include "llchat.h"
#include "llvoiceclient.h"
#include "lloutputmonitorctrl.h"
#include "llspeakers.h"
#include "llscrollbar.h"
#include "llviewerchat.h"
#include "llpanel.h"
#include "llemojidictionary.h"
#include "llfloateremojipicker.h"

class LLResizeBar;

class LLFloaterIMNearbyChat final
    :   public LLFloaterIMSessionTab
{
public:
    // constructor for inline chat-bars (e.g. hosted in chat history window)
    LLFloaterIMNearbyChat(const LLSD& key = LLSD(LLUUID()));
// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    ~LLFloaterIMNearbyChat();
// [/SL:KB]
//  ~LLFloaterIMNearbyChat() {}

    static LLFloaterIMNearbyChat* buildFloater(const LLSD& key);

    /*virtual*/ BOOL postBuild() override;
    /*virtual*/ void onOpen(const LLSD& key) override;
    /*virtual*/ void onClose(bool app_quitting) override;
    /*virtual*/ void setVisible(BOOL visible) override;
    /*virtual*/ void setVisibleAndFrontmost(BOOL take_focus=TRUE, const LLSD& key = LLSD()) override;
    /*virtual*/ void closeHostedFloater() override;

    void    closeFloater(bool app_quitting = false) override;

    void loadHistory();
    void reloadMessages(bool clean_messages = false);
    void removeScreenChat();

    void show();
    bool isMessagePanelVisible() const;
    bool isChatVisible() const;

    /** @param archive true - to save a message to the chat history log */
    void    addMessage          (const LLChat& message,bool archive = true, const LLSD &args = LLSD());

    LLChatEntry* getChatBox() { return mInputEditor; }

    std::string getCurrentChat();
    S32 getMessageArchiveLength() {return mMessageArchive.size();}

    BOOL handleKeyHere( KEY key, MASK mask ) override;

    static void startChat(const char* line);
    static void stopChat();

    static void sendChatFromViewer(const std::string &utf8text, EChatType type, BOOL animate);
    static void sendChatFromViewer(const LLWString &wtext, EChatType type, BOOL animate);

    template <class T>
    static void processChat(T* editor, EChatType type, std::function<void(const LLWString&)> func = nullptr);

    static bool isWordsName(const std::string& name);

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    /*virtual*/ void reshape(S32 width, S32 height, BOOL called_from_parent = TRUE) override;

    typedef boost::signals2::signal<void (LLUICtrl* ctrl, S32 width, S32 height)> reshape_signal_t;
    boost::signals2::connection setReshapeCallback(const reshape_signal_t::slot_type& cb);
// [/SL:KB]

    void showHistory();
    void changeChannelLabel(S32 channel);

protected:
    static BOOL matchChatTypeTrigger(const std::string& in_str, std::string* out_str);
    void onChatBoxKeystroke();
    void onChatBoxFocusLost();
    void onChatBoxFocusReceived();

    void sendChat( EChatType type );
    void onChatBoxCommit();
    void onChatFontChange(LLFontGL* fontp);

    /*virtual*/ void onTearOffClicked() override;
    /*virtual*/ void onClickCloseBtn(bool app_qutting = false) override;

public:
    static LLWString stripChannelNumber(const LLWString &mesg, S32* channel);
    static EChatType processChatTypeTriggers(EChatType type, std::string &str);

protected:
    void displaySpeakingIndicator();

    // Which non-zero channel did we last chat on?
    static S32 sLastSpecialChatChannel;

    LLOutputMonitorCtrl*    mOutputMonitor;
    LLLocalSpeakerMgr*      mSpeakerMgr;

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    reshape_signal_t*       mReshapeSignal;
// [/SL:KB]
    S32 mExpandedHeight;

private:
    /*virtual*/ void refresh() override;

    std::vector<LLChat> mMessageArchive;

    boost::signals2::connection mChatChannelConnection;
};

template <class T>
void LLFloaterIMNearbyChat::processChat(T* editor, EChatType type, std::function<void(const LLWString&)> emoji_func)
{
    if (editor)
    {
        LLWString text = editor->getWText();
        LLWStringUtil::trim(text);
        LLWStringUtil::replaceChar(text, 182, '\n'); // Convert paragraph symbols back into newlines.
        if (!text.empty())
        {
            if (emoji_func != nullptr)
            {
                emoji_func(text);
            }
            else
            {
                LLEmojiDictionary* dictionary = LLEmojiDictionary::getInstance();
                llassert_always(dictionary);

                bool emojiSent = false;
                for (llwchar& c : text)
                {
                    if (dictionary->isEmoji(c))
                    {
                        LLFloaterEmojiPicker::onEmojiUsed(c);
                        emojiSent = true;
                    }
                }

                if (emojiSent)
                    LLFloaterEmojiPicker::saveState();
            }

            // Check if this is destined for another channel
            S32 channel = 0;
            stripChannelNumber(text, &channel);

            std::string utf8text = wstring_to_utf8str(text);

            if (type == CHAT_TYPE_OOC)
            {
                utf8text = fmt::format("{} {} {}", gSavedSettings.getString("ChatOOCPrefix"), utf8text, gSavedSettings.getString("ChatOOCPostfix"));
            }

            // Try to trigger a gesture, if not chat to a script.
            std::string utf8_revised_text;
            if (0 == channel)
            {
                applyOOCClose(utf8text);
                applyMUPose(utf8text);

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
            type = processChatTypeTriggers(nType, utf8_revised_text);

            if (!utf8_revised_text.empty() && !ALChatCommand::parseCommand(utf8_revised_text))
            {
                // Chat with animation
                sendChatFromViewer(utf8_revised_text, type, gSavedSettings.getBOOL("PlayTypingAnim"));
            }
        }

        editor->setText(LLStringExplicit(""));
    }

    gAgent.stopTyping();
}

#endif // LL_LLFLOATERIMNEARBYCHAT_H
