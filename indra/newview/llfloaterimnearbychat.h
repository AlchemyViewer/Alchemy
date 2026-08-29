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

class LLResizeBar;

class LLFloaterIMNearbyChat
    :   public LLFloaterIMSessionTab
{
public:
    // constructor for inline chat-bars (e.g. hosted in chat history window)
    LLFloaterIMNearbyChat(const LLSD& key = LLSD(LLUUID()));
    ~LLFloaterIMNearbyChat();

    static LLFloaterIMNearbyChat* buildFloater(const LLSD& key);

    bool postBuild() override;
    void onOpen(const LLSD& key) override;
    void onClose(bool app_quitting) override;
    void setVisible(bool visible) override;
    void setVisibleAndFrontmost(bool take_focus=true, const LLSD& key = LLSD()) override;
    void closeHostedFloater() override;

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
    S32 getMessageArchiveLength() { return static_cast<S32>(mMessageArchive.size()); }

    bool handleKeyHere( KEY key, MASK mask ) override;

    static void startChat(const char* line);
    static void stopChat();

    static void sendChatFromViewer(const std::string &utf8text, EChatType type, bool animate);

    static bool isWordsName(const std::string& name);

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

    typedef boost::signals2::signal<void (LLUICtrl* ctrl, S32 width, S32 height)> reshape_signal_t;
    boost::signals2::connection setReshapeCallback(const reshape_signal_t::slot_type& cb);
// [/SL:KB]

    void showHistory();
    void changeChannelLabel(S32 channel);

protected:
    static bool matchChatTypeTrigger(const std::string& in_str, std::string* out_str);
    void onChatBoxKeystroke();
    void onChatBoxFocusLost();
    void onChatBoxFocusReceived();

    void sendChat( EChatType type );
    void onChatBoxCommit();
    void onChatFontChange(LLFontGL* fontp);

    void onTearOffClicked() override;
    void onClickCloseBtn(bool app_qutting = false) override;

public:
    static std::string stripChannelNumber(const std::string &mesg, S32* channel);
    static EChatType processChatTypeTriggers(EChatType type, std::string &str);

protected:
    void displaySpeakingIndicator();

// [RLVa:KB]
    void setChatMentionPickerEnabled(bool enabled);
    void updateRlvRestrictions(ERlvBehaviour behavior);
// [/RLVa:KB]

    // Which non-zero channel did we last chat on?
    static S32 sLastSpecialChatChannel;

    LLOutputMonitorCtrl*    mOutputMonitor;
    LLLocalSpeakerMgr*      mSpeakerMgr;

// [SL:KB] - Patch: Chat-NearbyToastWidth | Checked: 2010-11-10 (Catznip-2.4)
    reshape_signal_t*       mReshapeSignal;
// [/SL:KB]
    S32 mExpandedHeight;

// [RLVa:KB]
    boost::signals2::connection mRlvBehaviorCallbackConnection{};
// [/RLVa:KB]

private:
    void refresh() override;

    std::vector<LLChat> mMessageArchive;

    boost::signals2::connection mChatChannelConnection;
};

#endif // LL_LLFLOATERIMNEARBYCHAT_H
