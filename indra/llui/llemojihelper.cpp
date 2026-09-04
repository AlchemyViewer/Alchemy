/**
* @file llemojihelper.h
* @brief Header file for LLEmojiHelper
*
* $LicenseInfo:firstyear=2014&license=viewerlgpl$
* Second Life Viewer Source Code
* Copyright (C) 2014, Linden Research, Inc.
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

#include "linden_common.h"

#include "llemojidictionary.h"
#include "llemojihelper.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "lluictrl.h"

// ============================================================================
// Constants
//

constexpr char DEFAULT_EMOJI_HELPER_FLOATER[] = "emoji_picker";
constexpr S32 HELPER_FLOATER_OFFSET_X = 0;
constexpr S32 HELPER_FLOATER_OFFSET_Y = 0;

// ============================================================================
// LLEmojiHelper
//

std::string LLEmojiHelper::getToolTip(std::string_view emoji) const
{
    return LLEmojiDictionary::instance().getNameFromEmoji(emoji);
}

bool LLEmojiHelper::isActive(const LLUICtrl* ctrl_p) const
{
    return mHostHandle.get() == ctrl_p;
}

// static
bool LLEmojiHelper::isCursorInEmojiCode(std::string_view text, S32 cursorPos, S32* pShortCodePos)
{
    // The walk goes back a character at a time, not a byte at a time: the
    // predicate below classifies Unicode, and one byte of a multi-byte
    // character is not a codepoint. A shortcode is ASCII, so a multi-byte
    // character simply ends the run -- which is the right answer.
    auto prev_char = [&text](S32 pos) -> S32
    {
        S32 at = pos - 1;
        while (at > 0 && ((unsigned char)text[at] & 0xC0) == 0x80)
        {
            --at;
        }
        return at;
    };
    auto char_at = [&text](S32 pos) -> llwchar
    {
        return (pos >= 0 && pos < (S32)text.size())
            ? utf8str_decode_at(text, (size_t)pos).cp : 0;
    };

    // If the cursor is currently on a colon start the check one character further back
    S32 shortCodePos = cursorPos;
    if (cursorPos > 0 && U':' == char_at(prev_char(cursorPos)))
    {
        shortCodePos = prev_char(cursorPos);
    }

    auto isPartOfShortcode = [](llwchar ch) {
        switch (ch)
        {
            case U'-':
            case U'_':
            case U'+':
                return true;
            default:
                return LLStringOps::isAlnum(ch);
        }
    };
    while (shortCodePos > 0 && isPartOfShortcode(char_at(prev_char(shortCodePos))))
    {
        shortCodePos = prev_char(shortCodePos);
    }

    const S32 colonPos = prev_char(shortCodePos);
    bool isShortCode = (cursorPos - shortCodePos >= 2)
                    && (shortCodePos > 0) && (U':' == char_at(colonPos));
    // <TS:3T> Add qualifier to avoid emoji pop-up when typing times.
    if (isShortCode && (colonPos > 0) && LLStringOps::isDigit(char_at(prev_char(colonPos))))
        isShortCode = false;
    if (pShortCodePos)
        *pShortCodePos = (isShortCode) ? colonPos : -1;
    return isShortCode;
}

void LLEmojiHelper::showHelper(LLUICtrl* hostctrl_p, S32 local_x, S32 local_y, const std::string& short_code, std::function<void(const std::string&)> cb)
{
    // Commit immediately if the user already typed a full shortcode.
    // Variant shortcodes (e.g. :thumbs_up_dark_skin_tone:) need the
    // variant's character, not the base's, so we go through
    // getEmojiFromShortCode rather than reading descriptor->Character.
    const std::string emoji_chars =
        LLEmojiDictionary::instance().getEmojiFromShortCode(short_code);
    if (!emoji_chars.empty())
    {
        cb(emoji_chars);
        hideHelper();
        return;
    }

    if (mHelperHandle.isDead())
    {
        LLFloater* pHelperFloater = LLFloaterReg::getInstance(DEFAULT_EMOJI_HELPER_FLOATER);
        mHelperHandle = pHelperFloater->getHandle();
        // The picker serialises the chosen emoji as a UTF-8 string, which is
        // what the editors hold, so the whole sequence passes through as it is
        // and ZWJ families survive the callback.
        pHelperFloater->setCommitCallback(std::bind([&](const LLSD& sdValue) { onCommitEmoji(sdValue.asStringRef()); }, std::placeholders::_2));
        pHelperFloater->setCloseCallback([this](LLUICtrl* ctrl, const LLSD& param) { onCloseHelper(ctrl, param); });
    }
    setHostCtrl(hostctrl_p);
    mEmojiCommitCb = cb;

    S32 floater_x, floater_y;
    if (!hostctrl_p->localPointToOtherView(local_x, local_y, &floater_x, &floater_y, gFloaterView))
    {
        LL_ERRS() << "Cannot show emoji helper for non-floater controls." << LL_ENDL;
        return;
    }

    LLFloater* pHelperFloater = mHelperHandle.get();
    LLRect rect = pHelperFloater->getRect();
    S32 left = floater_x - HELPER_FLOATER_OFFSET_X;
    S32 top = floater_y - HELPER_FLOATER_OFFSET_Y + rect.getHeight();
    rect.setLeftTopAndSize(left, top, rect.getWidth(), rect.getHeight());
    pHelperFloater->setRect(rect);

    // Hack: Trying to open floater, search for a match,
    // and hide floater immediately if no match found,
    // instead of checking prior to opening
    //
    // Supress sounds in case floater won't be shown.
    // Todo: add some kind of shouldShow(short_code)
    U8 sound_flags = pHelperFloater->getSoundFlags();
    pHelperFloater->setSoundFlags(LLView::SILENT);
    pHelperFloater->openFloater(LLSD().with("hint", short_code));
    pHelperFloater->setSoundFlags(sound_flags);
}

void LLEmojiHelper::hideHelper(const LLUICtrl* ctrl_p, bool strict)
{
    mIsHideDisabled &= !strict;
    if (mIsHideDisabled || (ctrl_p && !isActive(ctrl_p)))
    {
        return;
    }

    setHostCtrl(nullptr);
}

bool LLEmojiHelper::handleKey(const LLUICtrl* ctrl_p, KEY key, MASK mask)
{
    if (mHelperHandle.isDead() || !isActive(ctrl_p))
    {
        return false;
    }

    return mHelperHandle.get()->handleKey(key, mask, true);
}

void LLEmojiHelper::onCommitEmoji(const std::string& emoji)
{
    if (!mHostHandle.isDead() && mEmojiCommitCb)
    {
        mEmojiCommitCb(emoji);
    }
}

void LLEmojiHelper::onCloseHelper(LLUICtrl* ctrl, const LLSD& param)
{
    mCloseSignal(ctrl, param);
}

boost::signals2::connection LLEmojiHelper::setCloseCallback(const commit_signal_t::slot_type& cb)
{
    return mCloseSignal.connect(cb);
}

void LLEmojiHelper::setHostCtrl(LLUICtrl* hostctrl_p)
{
    const LLUICtrl* pCurHostCtrl = mHostHandle.get();
    if (pCurHostCtrl != hostctrl_p)
    {
        mHostCtrlFocusLostConn.disconnect();
        mHostHandle.markDead();
        mEmojiCommitCb = {};

        if (!mHelperHandle.isDead())
        {
            mHelperHandle.get()->closeFloater();
        }

        if (hostctrl_p)
        {
            mHostHandle = hostctrl_p->getHandle();
            mHostCtrlFocusLostConn = hostctrl_p->setFocusLostCallback(std::bind([&]() { hideHelper(getHostCtrl()); }));
        }
    }
}
