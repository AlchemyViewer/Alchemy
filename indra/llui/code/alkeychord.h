/**
 * @file alkeychord.h
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

#ifndef AL_ALKEYCHORD_H
#define AL_ALKEYCHORD_H

#include "indra_constants.h"
#include "stdtypes.h"

#include <functional>
#include <string>

class LLView;

// A key as the studio's keymaps name it. The keypad's slash is KEY_DIVIDE,
// and was the slash key's too on SDL's windows -- the Mac's and Linux's --
// before they gave it '/', as Windows does; so a key bound as '/' is heard
// from either, and a binding kept as KEY_DIVIDE reads as '/'.
inline KEY alKeyAsBound(KEY key)
{
    return key == KEY_DIVIDE ? KEY('/') : key;
}

// A key and its modifiers, or two in turn -- Control-K, then S -- for the
// commands past what single keys hold without taking Control and Alt
// together, which is AltGr on many a keyboard, and types.
struct ALKeyChord
{
    KEY  key      = KEY_NONE;
    MASK mask     = MASK_NONE;
    // The key pressed first, for two; KEY_NONE for one.
    KEY  leadKey  = KEY_NONE;
    MASK leadMask = MASK_NONE;

    bool none() const { return key == KEY_NONE; }
    bool twoKeys() const { return leadKey != KEY_NONE; }
    // Whether a key pressed is the first of these two.
    bool ledBy(KEY key_pressed, MASK mask_pressed) const { return twoKeys() && leadKey == key_pressed && leadMask == mask_pressed; }
    bool operator==(const ALKeyChord& other) const = default;
    // As the platform writes keys, the two apart: "Ctrl+K S", or the
    // Mac's symbols. Empty for none.
    std::string describe() const;
};

// The one window waiting for the second key of two. Whatever has the
// keyboard would otherwise hear it first -- a text types a plain S, and
// takes Control-F for its own find -- and a first key left waiting would
// finish some later, unrelated chord. So the viewer asks here before the
// keyboard is given anything: the next key goes to the window waiting,
// whatever it is, and ends the wait.
namespace ALKeyChords
{
    // `window` has had the first key: the next is told to `second`. The
    // wait ends with it, with the window, or once the keyboard is
    // somewhere else than it is now.
    void wait(LLView* window, KEY lead_key, MASK lead_mask, std::function<void(KEY, MASK)> second);
    bool waiting();
    void stop();
    // A key pressed, to whoever waits for it: false where nobody does, or
    // it is a modifier or Caps Lock on its own. The first key again, held
    // down, is taken and changes nothing. A key that types takes its
    // character with it: the next one, where it comes the same frame
    // (takeChar).
    bool takeKey(KEY key, MASK mask);
    bool takeChar(llwchar uni_char);
    // Shift-F10, or the Menu key: what opens the menu a right click would,
    // where the keyboard is.
    inline bool isContextMenuKey(KEY key, MASK mask)
    {
        return (key == KEY_F10 && mask == MASK_SHIFT) || (key == KEY_CONTEXT_MENU && mask == MASK_NONE);
    }
}

#endif // AL_ALKEYCHORD_H
