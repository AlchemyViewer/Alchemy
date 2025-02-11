/**
 * @file llkeyboardwin32.cpp
 * @brief Handler for assignable key bindings
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#if LL_WINDOWS

#include "linden_common.h"

#include "llwin32headers.h"
#include "llkeyboardwin32.h"

#include "llwindowcallbacks.h"



LLKeyboardWin32::LLKeyboardWin32()
{
    // Set up key mapping for windows - eventually can read this from a file?
    // Anything not in the key map gets dropped
    // Add default A-Z

    // Virtual key mappings from WinUser.h

    KEY cur_char;
    for (cur_char = 'A'; cur_char <= 'Z'; cur_char++)
    {
        mTranslateKeyMap[cur_char] = (KEY)cur_char;
    }

    for (cur_char = '0'; cur_char <= '9'; cur_char++)
    {
        mTranslateKeyMap[cur_char] = (KEY)cur_char;
    }
    // numpad number keys
    for (cur_char = 0x60; cur_char <= 0x69; cur_char++)
    {
        mTranslateKeyMap[cur_char] = (KEY)('0' + (cur_char - 0x60));
    }


    mTranslateKeyMap[VK_SPACE] = ' ';
    mTranslateKeyMap[VK_OEM_1] = ';';
    // When the user hits, for example, Ctrl-= as a keyboard shortcut,
    // Windows generates VK_OEM_PLUS.  This is true on both QWERTY and DVORAK
    // keyboards in the US.  Numeric keypad '+' generates VK_ADD below.
    // Thus we translate it as '='.
    // Potential bug: This may not be true on international keyboards. JC
    mTranslateKeyMap[VK_OEM_PLUS]   = '=';
    mTranslateKeyMap[VK_OEM_COMMA]  = ',';
    mTranslateKeyMap[VK_OEM_MINUS]  = '-';
    mTranslateKeyMap[VK_OEM_PERIOD] = '.';
    mTranslateKeyMap[VK_OEM_2] = '/';//This used to be KEY_PAD_DIVIDE, but that breaks typing into text fields in media prims
    mTranslateKeyMap[VK_OEM_3] = '`';
    mTranslateKeyMap[VK_OEM_4] = '[';
    mTranslateKeyMap[VK_OEM_5] = '\\';
    mTranslateKeyMap[VK_OEM_6] = ']';
    mTranslateKeyMap[VK_OEM_7] = '\'';
    mTranslateKeyMap[VK_ESCAPE] = KEY_ESCAPE;
    mTranslateKeyMap[VK_RETURN] = KEY_RETURN;
    mTranslateKeyMap[VK_LEFT] = KEY_LEFT;
    mTranslateKeyMap[VK_RIGHT] = KEY_RIGHT;
    mTranslateKeyMap[VK_UP] = KEY_UP;
    mTranslateKeyMap[VK_DOWN] = KEY_DOWN;
    mTranslateKeyMap[VK_BACK] = KEY_BACKSPACE;
    mTranslateKeyMap[VK_INSERT] = KEY_INSERT;
    mTranslateKeyMap[VK_DELETE] = KEY_DELETE;
    mTranslateKeyMap[VK_SHIFT] = KEY_SHIFT;
    mTranslateKeyMap[VK_CONTROL] = KEY_CONTROL;
    mTranslateKeyMap[VK_MENU] = KEY_ALT;
    mTranslateKeyMap[VK_CAPITAL] = KEY_CAPSLOCK;
    mTranslateKeyMap[VK_HOME] = KEY_HOME;
    mTranslateKeyMap[VK_END] = KEY_END;
    mTranslateKeyMap[VK_PRIOR] = KEY_PAGE_UP;
    mTranslateKeyMap[VK_NEXT] = KEY_PAGE_DOWN;
    mTranslateKeyMap[VK_TAB] = KEY_TAB;
    mTranslateKeyMap[VK_ADD] = KEY_ADD;
    mTranslateKeyMap[VK_SUBTRACT] = KEY_SUBTRACT;
    mTranslateKeyMap[VK_MULTIPLY] = KEY_MULTIPLY;
    mTranslateKeyMap[VK_DIVIDE] = KEY_DIVIDE;
    mTranslateKeyMap[VK_F1] = KEY_F1;
    mTranslateKeyMap[VK_F2] = KEY_F2;
    mTranslateKeyMap[VK_F3] = KEY_F3;
    mTranslateKeyMap[VK_F4] = KEY_F4;
    mTranslateKeyMap[VK_F5] = KEY_F5;
    mTranslateKeyMap[VK_F6] = KEY_F6;
    mTranslateKeyMap[VK_F7] = KEY_F7;
    mTranslateKeyMap[VK_F8] = KEY_F8;
    mTranslateKeyMap[VK_F9] = KEY_F9;
    mTranslateKeyMap[VK_F10] = KEY_F10;
    mTranslateKeyMap[VK_F11] = KEY_F11;
    mTranslateKeyMap[VK_F12] = KEY_F12;
    mTranslateKeyMap[VK_CLEAR] = KEY_PAD_CENTER;

    // Build inverse map
    for (auto iter = mTranslateKeyMap.begin(); iter != mTranslateKeyMap.end(); iter++)
    {
        mInvTranslateKeyMap[iter->second] = iter->first;
    }

    // numpad map
    mTranslateNumpadMap[VK_NUMPAD0] = KEY_PAD_INS;  // keypad 0
    mTranslateNumpadMap[VK_NUMPAD1] = KEY_PAD_END;  // keypad 1
    mTranslateNumpadMap[VK_NUMPAD2] = KEY_PAD_DOWN; // keypad 2
    mTranslateNumpadMap[VK_NUMPAD3] = KEY_PAD_PGDN; // keypad 3
    mTranslateNumpadMap[VK_NUMPAD4] = KEY_PAD_LEFT; // keypad 4
    mTranslateNumpadMap[VK_NUMPAD5] = KEY_PAD_CENTER;   // keypad 5
    mTranslateNumpadMap[VK_NUMPAD6] = KEY_PAD_RIGHT;    // keypad 6
    mTranslateNumpadMap[VK_NUMPAD7] = KEY_PAD_HOME; // keypad 7
    mTranslateNumpadMap[VK_NUMPAD8] = KEY_PAD_UP;       // keypad 8
    mTranslateNumpadMap[VK_NUMPAD9] = KEY_PAD_PGUP; // keypad 9
    mTranslateNumpadMap[VK_MULTIPLY] = KEY_PAD_MULTIPLY;    // keypad *
    mTranslateNumpadMap[VK_ADD] = KEY_PAD_ADD;  // keypad +
    mTranslateNumpadMap[VK_SUBTRACT] = KEY_PAD_SUBTRACT;    // keypad -
    mTranslateNumpadMap[VK_DECIMAL] = KEY_PAD_DEL;  // keypad .
    mTranslateNumpadMap[VK_DIVIDE] = KEY_PAD_DIVIDE;    // keypad /

    for (auto iter = mTranslateNumpadMap.begin(); iter != mTranslateNumpadMap.end(); iter++)
    {
        mInvTranslateNumpadMap[iter->second] = iter->first;
    }
}

// Asynchronously poll the control, alt and shift keys and set the
// appropriate states.
// Note: this does not generate edges.
void LLKeyboardWin32::resetMaskKeys()
{
    // GetAsyncKeyState returns a short and uses the most significant
    // bit to indicate that the key is down.
    if (GetAsyncKeyState(VK_SHIFT) & 0x8000)
    {
        mKeyLevel[KEY_SHIFT] = true;
    }

    if (GetAsyncKeyState(VK_CONTROL) & 0x8000)
    {
        mKeyLevel[KEY_CONTROL] = true;
    }

    if (GetAsyncKeyState(VK_MENU) & 0x8000)
    {
        mKeyLevel[KEY_ALT] = true;
    }
}


//void LLKeyboardWin32::setModifierKeyLevel( KEY key, bool new_state )
//{
//  if( mKeyLevel[key] != new_state )
//  {
//      mKeyLevelFrameCount[key] = 0;
//
//      if( new_state )
//      {
//          mKeyLevelTimer[key].reset();
//      }
//      mKeyLevel[key] = new_state;
//  }
//}


MASK LLKeyboardWin32::updateModifiers()
{
    //RN: this seems redundant, as we should have already received the appropriate
    // messages for the modifier keys

    // Scan the modifier keys as of the last Windows key message
    // (keydown encoded in high order bit of short)
    mKeyLevel[KEY_CAPSLOCK] = (GetKeyState(VK_CAPITAL) & 0x0001) != 0; // Low order bit carries the toggle state.
    // Get mask for keyboard events
    MASK mask = currentMask(false);
    return mask;
}


// mask is ignored, except for extended flag -- we poll the modifier keys for the other flags
bool LLKeyboardWin32::handleKeyDown(const U32 key, MASK mask)
{
    KEY     translated_key;
    U32     translated_mask;
    bool    handled = false;

    translated_mask = updateModifiers();

    if (translateExtendedKey(key, mask, &translated_key))
    {
        handled = handleTranslatedKeyDown(translated_key, translated_mask);
    }

    return handled;
}

// mask is ignored, except for extended flag -- we poll the modifier keys for the other flags
bool LLKeyboardWin32::handleKeyUp(const U32 key, MASK mask)
{
    KEY     translated_key;
    U32     translated_mask;
    bool    handled = false;

    translated_mask = updateModifiers();

    if (translateExtendedKey(key, mask, &translated_key))
    {
        handled = handleTranslatedKeyUp(translated_key, translated_mask);
    }

    return handled;
}


MASK LLKeyboardWin32::currentMask(bool)
{
    MASK mask = MASK_NONE;

    if (mKeyLevel[KEY_SHIFT])       mask |= MASK_SHIFT;
    if (mKeyLevel[KEY_CONTROL])     mask |= MASK_CONTROL;
    if (mKeyLevel[KEY_ALT])         mask |= MASK_ALT;

    return mask;
}


void LLKeyboardWin32::scanKeyboard()
{
    S32 key;
    MSG msg;
    PeekMessage(&msg, NULL, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE | PM_NOYIELD);
    for (key = 0; key < KEY_COUNT; key++)
    {
        // Generate callback if any event has occurred on this key this frame.
        // Can't just test mKeyLevel, because this could be a slow frame and
        // key might have gone down then up. JC
        if (mKeyLevel[key] || mKeyDown[key] || mKeyUp[key])
        {
            mCurScanKey = key;
            mCallbacks->handleScanKey(key, mKeyDown[key], mKeyUp[key], mKeyLevel[key]);
        }
    }

    // Reset edges for next frame
    for (key = 0; key < KEY_COUNT; key++)
    {
        mKeyUp[key] = false;
        mKeyDown[key] = false;
        if (mKeyLevel[key])
        {
            mKeyLevelFrameCount[key]++;
        }
    }
}

bool LLKeyboardWin32::translateExtendedKey(const U32 os_key, const MASK mask, KEY *translated_key)
{
    return translateKey(os_key, translated_key);
}

U32  LLKeyboardWin32::inverseTranslateExtendedKey(const KEY translated_key)
{
    // if numlock is on, then we need to translate KEY_PAD_FOO to the corresponding number pad number
    if(GetKeyState(VK_NUMLOCK) & 1)
    {
        auto iter = mInvTranslateNumpadMap.find(translated_key);
        if (iter != mInvTranslateNumpadMap.end())
        {
            return iter->second;
        }
    }

    // if numlock is off or we're not converting numbers to arrows, we map our keypad arrows
    // to regular arrows since Windows doesn't distinguish between them
    KEY converted_key = translated_key;
    switch (converted_key)
    {
        case KEY_PAD_LEFT:
            converted_key = KEY_LEFT; break;
        case KEY_PAD_RIGHT:
            converted_key = KEY_RIGHT; break;
        case KEY_PAD_UP:
            converted_key = KEY_UP; break;
        case KEY_PAD_DOWN:
            converted_key = KEY_DOWN; break;
        case KEY_PAD_HOME:
            converted_key = KEY_HOME; break;
        case KEY_PAD_END:
            converted_key = KEY_END; break;
        case KEY_PAD_PGUP:
            converted_key = KEY_PAGE_UP; break;
        case KEY_PAD_PGDN:
            converted_key = KEY_PAGE_DOWN; break;
        case KEY_PAD_INS:
            converted_key = KEY_INSERT; break;
        case KEY_PAD_DEL:
            converted_key = KEY_DELETE; break;
        case KEY_PAD_RETURN:
            converted_key = KEY_RETURN; break;
    }
    // convert our virtual keys to OS keys
    return inverseTranslateKey(converted_key);
}

#endif
