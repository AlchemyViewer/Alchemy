/**
 * @file alkeychord.cpp
 * @brief A key, or two in turn, and the window waiting for the second.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
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
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "alkeychord.h"

#include "llfocusmgr.h"
#include "llframetimer.h"
#include "llkeyboard.h"
#include "llview.h"

#include <optional>
#include <utility>

std::string ALKeyChord::describe() const
{
    if (none())
    {
        return std::string();
    }
    const std::string second = LLKeyboard::stringFromAccelerator(mask, key);
    return twoKeys() ? LLKeyboard::stringFromAccelerator(leadMask, leadKey) + " " + second : second;
}

namespace
{
    struct Wait
    {
        LLHandle<LLView>                window;
        // What had the keyboard as the first key was pressed.
        const LLFocusableElement*       focus    = nullptr;
        KEY                             leadKey  = KEY_NONE;
        MASK                            leadMask = MASK_NONE;
        std::function<void(KEY, MASK)>  second;
    };
    Wait sWait;
    // The frame a taken key typing a character was pressed in, whose
    // character is taken with it; none while there is none.
    std::optional<U32> sTakenFrame;

    bool live()
    {
        if (!sWait.second)
        {
            return false;
        }
        if (sWait.window.isDead() || gFocusMgr.getKeyboardFocus() != sWait.focus)
        {
            ALKeyChords::stop();
            return false;
        }
        return true;
    }
}

namespace ALKeyChords
{
    void wait(LLView* window, KEY lead_key, MASK lead_mask, std::function<void(KEY, MASK)> second)
    {
        sWait.window   = window ? window->getHandle() : LLHandle<LLView>();
        sWait.focus    = gFocusMgr.getKeyboardFocus();
        sWait.leadKey  = lead_key;
        sWait.leadMask = lead_mask;
        sWait.second   = std::move(second);
    }

    bool waiting() { return live(); }

    void stop() { sWait = Wait(); }

    bool takeKey(KEY pressed, MASK mask)
    {
        // A modifier, or Caps Lock pressed to type capitals, is no second
        // key: the wait goes on to the key it is held or toggled for.
        if (!live() || pressed == KEY_SHIFT || pressed == KEY_CONTROL || pressed == KEY_ALT || pressed == KEY_CAPSLOCK)
        {
            return false;
        }
        const KEY key = alKeyAsBound(pressed);
        if (key == sWait.leadKey && mask == sWait.leadMask && gKeyboard && gKeyboard->getKeyRepeated(pressed))
        {
            // The first key still held: nothing new yet.
            return true;
        }
        // A key a text would have typed, held with nothing or with Shift:
        // its character follows it, and is not to be typed either.
        if ((mask & (MASK_CONTROL | MASK_ALT | MASK_MAC_CONTROL)) == 0 && key >= 0x20 && key < 0x80)
        {
            sTakenFrame = LLFrameTimer::getFrameCount();
        }
        const std::function<void(KEY, MASK)> second = std::move(sWait.second);
        stop();
        second(key, mask);
        return true;
    }

    bool takeChar(llwchar uni_char)
    {
        if (!sTakenFrame)
        {
            return false;
        }
        // Its own character only, which comes straight after it; one
        // typed after that is the next key's.
        const bool same_frame = *sTakenFrame == LLFrameTimer::getFrameCount();
        sTakenFrame.reset();
        return same_frame;
    }
}
