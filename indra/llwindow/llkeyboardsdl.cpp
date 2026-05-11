/**
 * @file llkeyboardsdl.cpp
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

#include "linden_common.h"
#include "llkeyboardsdl.h"
#include "llwindowcallbacks.h"

#include "SDL3/SDL_keycode.h"

LLKeyboardSDL::LLKeyboardSDL()
{
    // Set up key mapping for SDL - eventually can read this from a file?
    // Anything not in the key map gets dropped
    // Add default A-Z

    // Virtual key mappings from SDL_keysym.h ...

    // SDL maps the letter keys to the ASCII you'd expect, but it's lowercase...
    LLKeyboard::NATIVE_KEY_TYPE cur_char;
    for (cur_char = 'A'; cur_char <= 'Z'; cur_char++)
    {
        mTranslateKeyMap[cur_char] = cur_char;
    }
    for (cur_char = 'a'; cur_char <= 'z'; cur_char++)
    {
        mTranslateKeyMap[cur_char] = (cur_char - 'a') + 'A';
    }

    for (cur_char = '0'; cur_char <= '9'; cur_char++)
    {
        mTranslateKeyMap[cur_char] = cur_char;
    }

    // Numpad digits when NumLock is on. With NumLock off, adjustNativekeyFromUnhandledMask
    // below remaps SDLK_KP_<n> onto SDLK_LEFT/RIGHT/HOME/etc. before this map is consulted,
    // so this only fires for NumLock-on. Map to the ASCII digit values (KEY_0..KEY_9 share
    // the same constants — see indra_constants.h) so a numpad digit drives the same
    // KEY_DOWN as the top-row digit. Win32 does the equivalent at llkeyboardwin32.cpp:57-60;
    // without this loop numpad digits silently produced no KEY_DOWN at all on SDL3.
    mTranslateKeyMap[SDLK_KP_0] = '0';
    mTranslateKeyMap[SDLK_KP_1] = '1';
    mTranslateKeyMap[SDLK_KP_2] = '2';
    mTranslateKeyMap[SDLK_KP_3] = '3';
    mTranslateKeyMap[SDLK_KP_4] = '4';
    mTranslateKeyMap[SDLK_KP_5] = '5';
    mTranslateKeyMap[SDLK_KP_6] = '6';
    mTranslateKeyMap[SDLK_KP_7] = '7';
    mTranslateKeyMap[SDLK_KP_8] = '8';
    mTranslateKeyMap[SDLK_KP_9] = '9';

    mTranslateKeyMap[SDLK_SPACE] = ' ';
    mTranslateKeyMap[SDLK_RETURN] = KEY_RETURN;
    mTranslateKeyMap[SDLK_LEFT] = KEY_LEFT;
    mTranslateKeyMap[SDLK_RIGHT] = KEY_RIGHT;
    mTranslateKeyMap[SDLK_UP] = KEY_UP;
    mTranslateKeyMap[SDLK_DOWN] = KEY_DOWN;
    mTranslateKeyMap[SDLK_KP_ENTER] = KEY_RETURN;
    mTranslateKeyMap[SDLK_ESCAPE] = KEY_ESCAPE;
    mTranslateKeyMap[SDLK_BACKSPACE] = KEY_BACKSPACE;
    mTranslateKeyMap[SDLK_DELETE] = KEY_DELETE;
    mTranslateKeyMap[SDLK_LSHIFT] = KEY_SHIFT;
    mTranslateKeyMap[SDLK_RSHIFT] = KEY_SHIFT;
    mTranslateKeyMap[SDLK_LCTRL] = KEY_CONTROL;
    mTranslateKeyMap[SDLK_RCTRL] = KEY_CONTROL;
    mTranslateKeyMap[SDLK_LALT] = KEY_ALT;
    mTranslateKeyMap[SDLK_RALT] = KEY_ALT;
    mTranslateKeyMap[SDLK_HOME] = KEY_HOME;
    mTranslateKeyMap[SDLK_END] = KEY_END;
    mTranslateKeyMap[SDLK_PAGEUP] = KEY_PAGE_UP;
    mTranslateKeyMap[SDLK_PAGEDOWN] = KEY_PAGE_DOWN;
    mTranslateKeyMap[SDLK_MINUS] = KEY_HYPHEN;
    mTranslateKeyMap[SDLK_EQUALS] = KEY_EQUALS;
    mTranslateKeyMap[SDLK_KP_EQUALS] = KEY_EQUALS;
    mTranslateKeyMap[SDLK_INSERT] = KEY_INSERT;
    // KP_5 with NumLock off remaps to SDLK_CLEAR via adjustNativekeyFromUnhandledMask
    // below. This entry lets the binding system reach KEY_PAD_CENTER through that
    // remap — matching Win32's VK_CLEAR -> KEY_PAD_CENTER (llkeyboardwin32.cpp:114).
    mTranslateKeyMap[SDLK_CLEAR] = KEY_PAD_CENTER;
    mTranslateKeyMap[SDLK_CAPSLOCK] = KEY_CAPSLOCK;
    mTranslateKeyMap[SDLK_TAB] = KEY_TAB;
    mTranslateKeyMap[SDLK_KP_PLUS] = KEY_ADD;
    mTranslateKeyMap[SDLK_KP_MINUS] = KEY_SUBTRACT;
    mTranslateKeyMap[SDLK_KP_MULTIPLY] = KEY_MULTIPLY;
    mTranslateKeyMap[SDLK_KP_DIVIDE] = KEY_PAD_DIVIDE;
    mTranslateKeyMap[SDLK_F1] = KEY_F1;
    mTranslateKeyMap[SDLK_F2] = KEY_F2;
    mTranslateKeyMap[SDLK_F3] = KEY_F3;
    mTranslateKeyMap[SDLK_F4] = KEY_F4;
    mTranslateKeyMap[SDLK_F5] = KEY_F5;
    mTranslateKeyMap[SDLK_F6] = KEY_F6;
    mTranslateKeyMap[SDLK_F7] = KEY_F7;
    mTranslateKeyMap[SDLK_F8] = KEY_F8;
    mTranslateKeyMap[SDLK_F9] = KEY_F9;
    mTranslateKeyMap[SDLK_F10] = KEY_F10;
    mTranslateKeyMap[SDLK_F11] = KEY_F11;
    mTranslateKeyMap[SDLK_F12] = KEY_F12;
    mTranslateKeyMap[SDLK_PLUS]   = '=';
    mTranslateKeyMap[SDLK_COMMA]  = ',';
    mTranslateKeyMap[SDLK_MINUS]  = '-';
    mTranslateKeyMap[SDLK_PERIOD] = '.';
    mTranslateKeyMap[SDLK_GRAVE] = '`';
    mTranslateKeyMap[SDLK_SLASH] = KEY_DIVIDE;
    mTranslateKeyMap[SDLK_SEMICOLON] = ';';
    mTranslateKeyMap[SDLK_LEFTBRACKET] = '[';
    mTranslateKeyMap[SDLK_BACKSLASH] = '\\';
    mTranslateKeyMap[SDLK_RIGHTBRACKET] = ']';
    mTranslateKeyMap[SDLK_APOSTROPHE] = '\'';

    // Build inverse map
    for (auto iter = mTranslateKeyMap.begin(); iter != mTranslateKeyMap.end(); iter++)
    {
        mInvTranslateKeyMap[iter->second] = iter->first;
    }

    // numpad map
    mTranslateNumpadMap[SDLK_KP_0] = KEY_PAD_INS;
    mTranslateNumpadMap[SDLK_KP_1] = KEY_PAD_END;
    mTranslateNumpadMap[SDLK_KP_2] = KEY_PAD_DOWN;
    mTranslateNumpadMap[SDLK_KP_3] = KEY_PAD_PGDN;
    mTranslateNumpadMap[SDLK_KP_4] = KEY_PAD_LEFT;
    mTranslateNumpadMap[SDLK_KP_5] = KEY_PAD_CENTER;
    mTranslateNumpadMap[SDLK_KP_6] = KEY_PAD_RIGHT;
    mTranslateNumpadMap[SDLK_KP_7] = KEY_PAD_HOME;
    mTranslateNumpadMap[SDLK_KP_8] = KEY_PAD_UP;
    mTranslateNumpadMap[SDLK_KP_9] = KEY_PAD_PGUP;
    mTranslateNumpadMap[SDLK_KP_PERIOD] = KEY_PAD_DEL;

    // build inverse numpad map
    for (auto iter = mTranslateNumpadMap.begin();
         iter != mTranslateNumpadMap.end();
         iter++)
    {
        mInvTranslateNumpadMap[iter->second] = iter->first;
    }
}

void LLKeyboardSDL::resetMaskKeys()
{
    // Re-sync modifier levels to match SDL's authoritative mod-state.
    // The previous version only *set* bits, never cleared them, so a key
    // released while the viewer wasn't focused stayed "stuck on" — the
    // exact failure mode the comment used to suspect aloud.
    const SDL_Keymod mask = SDL_GetModState();

    mKeyLevel[KEY_SHIFT]   = (mask & SDL_KMOD_SHIFT) != 0;
    mKeyLevel[KEY_CONTROL] = (mask & SDL_KMOD_CTRL)  != 0;
    mKeyLevel[KEY_ALT]     = (mask & SDL_KMOD_ALT)   != 0;
}


MASK LLKeyboardSDL::updateModifiers(const MASK mask)
{
    // translate the mask
    MASK out_mask = MASK_NONE;

    if(mask & SDL_KMOD_SHIFT)
    {
        out_mask |= MASK_SHIFT;
    }

    if(mask & SDL_KMOD_CTRL)
    {
        out_mask |= MASK_CONTROL;
    }

    if(mask & SDL_KMOD_ALT)
    {
        out_mask |= MASK_ALT;
    }

    return out_mask;
}


LLKeyboard::NATIVE_KEY_TYPE adjustNativekeyFromUnhandledMask(const LLKeyboard::NATIVE_KEY_TYPE key, const MASK mask)
{
    // SDL doesn't automatically adjust the keysym according to
    // whether NUMLOCK is engaged, so we massage the keysym manually.
    LLKeyboard::NATIVE_KEY_TYPE rtn = key;
    if (!(mask & SDL_KMOD_NUM))
    {
        switch (key)
        {
            case SDLK_KP_PERIOD:  rtn = SDLK_DELETE; break;
            case SDLK_KP_DECIMAL: rtn = SDLK_DELETE; break;
            case SDLK_KP_0: rtn = SDLK_INSERT; break;
            case SDLK_KP_1: rtn = SDLK_END; break;
            case SDLK_KP_2: rtn = SDLK_DOWN; break;
            case SDLK_KP_3: rtn = SDLK_PAGEDOWN; break;
            case SDLK_KP_4: rtn = SDLK_LEFT; break;
            // KP_5 with NumLock off maps to SDLK_CLEAR; mTranslateKeyMap then
            // routes that to KEY_PAD_CENTER so the binding system can reach
            // the one numpad key that has no arrow / nav equivalent. Mirrors
            // Win32, where the OS sends VK_CLEAR for that physical key.
            case SDLK_KP_5: rtn = SDLK_CLEAR; break;
            case SDLK_KP_6: rtn = SDLK_RIGHT; break;
            case SDLK_KP_7: rtn = SDLK_HOME; break;
            case SDLK_KP_8: rtn = SDLK_UP; break;
            case SDLK_KP_9: rtn = SDLK_PAGEUP; break;
        }
    }
    return rtn;
}


bool LLKeyboardSDL::handleKeyDown(const LLKeyboard::NATIVE_KEY_TYPE key, const MASK mask)
{
    LLKeyboard::NATIVE_KEY_TYPE adjusted_nativekey;
    KEY translated_key = 0;
    MASK translated_mask = MASK_NONE;
    bool handled = false;

    adjusted_nativekey = adjustNativekeyFromUnhandledMask(key, mask);

    translated_mask = updateModifiers(mask);

    if(translateNumpadKey(adjusted_nativekey, &translated_key))
    {
        handled = handleTranslatedKeyDown(translated_key, translated_mask);
    }

    return handled;
}


bool LLKeyboardSDL::handleKeyUp(const LLKeyboard::NATIVE_KEY_TYPE key, const MASK mask)
{
    LLKeyboard::NATIVE_KEY_TYPE adjusted_nativekey;
    KEY translated_key = 0;
    U32 translated_mask = MASK_NONE;
    bool handled = false;

    adjusted_nativekey = adjustNativekeyFromUnhandledMask(key, mask);

    translated_mask = updateModifiers(mask);

    if(translateNumpadKey(adjusted_nativekey, &translated_key))
    {
        handled = handleTranslatedKeyUp(translated_key, translated_mask);
    }

    return handled;
}

MASK LLKeyboardSDL::currentMask(bool /*for_mouse_event*/)
{
    MASK result = MASK_NONE;
    const SDL_Keymod mask = SDL_GetModState();

    if (mask & SDL_KMOD_SHIFT)
        result |= MASK_SHIFT;
    if (mask & SDL_KMOD_CTRL)
        result |= MASK_CONTROL;
    if (mask & SDL_KMOD_ALT)
        result |= MASK_ALT;

    // SDL_KMOD_GUI is intentionally NOT translated to MASK_CONTROL here.
    // The macOS backend (llkeyboardmacosx.cpp) does the equivalent for
    // MAC_CMD_KEY because Cmd is the OS's primary command modifier — but
    // the SDL3 backend is Linux-only (LL_SDL_WINDOW gates it), and on
    // Linux SDL_KMOD_GUI is the Super / Mod4 key, which is reserved for
    // the window manager (Activities, workspace switching, app launching).
    // Treating Super as Control would cause Super+letter chords to fire
    // viewer Ctrl+letter shortcuts the user didn't ask for.
    //
    // The `for_mouse_event` argument is preserved to match the
    // LLKeyboard::currentMask base signature shared across backends, but
    // there is no per-event-type difference on Linux.

    return result;
}

void LLKeyboardSDL::scanKeyboard()
{
    for (S32 key = 0; key < KEY_COUNT; key++)
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
    mCurScanKey = KEY_NONE;

    // Reset edges for next frame
    for (S32 key = 0; key < KEY_COUNT; key++)
    {
        mKeyUp[key] = false;
        mKeyDown[key] = false;
        if (mKeyLevel[key])
        {
            mKeyLevelFrameCount[key]++;
        }
    }
}


bool LLKeyboardSDL::translateNumpadKey( const LLKeyboard::NATIVE_KEY_TYPE os_key, KEY *translated_key)
{
    return translateKey(os_key, translated_key);
}

LLKeyboard::NATIVE_KEY_TYPE LLKeyboardSDL::inverseTranslateNumpadKey(const KEY translated_key)
{
    return inverseTranslateKey(translated_key);
}

enum class WindowsVK : U32
{
    LL_VK_UNKNOWN = 0,
    LL_VK_CANCEL = 0x03,
    LL_VK_BACK = 0x08,
    LL_VK_TAB = 0x09,
    LL_VK_CLEAR = 0x0C,
    LL_VK_RETURN = 0x0D,
    LL_VK_SHIFT = 0x10,
    LL_VK_CONTROL = 0x11,
    LL_VK_MENU = 0x12,
    LL_VK_PAUSE = 0x13,
    LL_VK_CAPITAL = 0x14,
    LL_VK_KANA = 0x15,
    LL_VK_HANGUL = 0x15,
    LL_VK_JUNJA = 0x17,
    LL_VK_FINAL = 0x18,
    LL_VK_HANJA = 0x19,
    LL_VK_KANJI = 0x19,
    LL_VK_ESCAPE = 0x1B,
    LL_VK_CONVERT = 0x1C,
    LL_VK_NONCONVERT = 0x1D,
    LL_VK_ACCEPT = 0x1E,
    LL_VK_MODECHANGE = 0x1F,
    LL_VK_SPACE = 0x20,
    LL_VK_PRIOR = 0x21,
    LL_VK_NEXT = 0x22,
    LL_VK_END = 0x23,
    LL_VK_HOME = 0x24,
    LL_VK_LEFT = 0x25,
    LL_VK_UP = 0x26,
    LL_VK_RIGHT = 0x27,
    LL_VK_DOWN = 0x28,
    LL_VK_SELECT = 0x29,
    LL_VK_PRINT = 0x2A,
    LL_VK_EXECUTE = 0x2B,
    LL_VK_SNAPSHOT = 0x2C,
    LL_VK_INSERT = 0x2D,
    LL_VK_DELETE = 0x2E,
    LL_VK_HELP = 0x2F,
    LL_VK_0 = 0x30,
    LL_VK_1 = 0x31,
    LL_VK_2 = 0x32,
    LL_VK_3 = 0x33,
    LL_VK_4 = 0x34,
    LL_VK_5 = 0x35,
    LL_VK_6 = 0x36,
    LL_VK_7 = 0x37,
    LL_VK_8 = 0x38,
    LL_VK_9 = 0x39,
    LL_VK_A = 0x41,
    LL_VK_B = 0x42,
    LL_VK_C = 0x43,
    LL_VK_D = 0x44,
    LL_VK_E = 0x45,
    LL_VK_F = 0x46,
    LL_VK_G = 0x47,
    LL_VK_H = 0x48,
    LL_VK_I = 0x49,
    LL_VK_J = 0x4A,
    LL_VK_K = 0x4B,
    LL_VK_L = 0x4C,
    LL_VK_M = 0x4D,
    LL_VK_N = 0x4E,
    LL_VK_O = 0x4F,
    LL_VK_P = 0x50,
    LL_VK_Q = 0x51,
    LL_VK_R = 0x52,
    LL_VK_S = 0x53,
    LL_VK_T = 0x54,
    LL_VK_U = 0x55,
    LL_VK_V = 0x56,
    LL_VK_W = 0x57,
    LL_VK_X = 0x58,
    LL_VK_Y = 0x59,
    LL_VK_Z = 0x5A,
    LL_VK_LWIN = 0x5B,
    LL_VK_RWIN = 0x5C,
    LL_VK_APPS = 0x5D,
    LL_VK_SLEEP = 0x5F,
    LL_VK_NUMPAD0 = 0x60,
    LL_VK_NUMPAD1 = 0x61,
    LL_VK_NUMPAD2 = 0x62,
    LL_VK_NUMPAD3 = 0x63,
    LL_VK_NUMPAD4 = 0x64,
    LL_VK_NUMPAD5 = 0x65,
    LL_VK_NUMPAD6 = 0x66,
    LL_VK_NUMPAD7 = 0x67,
    LL_VK_NUMPAD8 = 0x68,
    LL_VK_NUMPAD9 = 0x69,
    LL_VK_MULTIPLY = 0x6A,
    LL_VK_ADD = 0x6B,
    LL_VK_SEPARATOR = 0x6C,
    LL_VK_SUBTRACT = 0x6D,
    LL_VK_DECIMAL = 0x6E,
    LL_VK_DIVIDE = 0x6F,
    LL_VK_F1 = 0x70,
    LL_VK_F2 = 0x71,
    LL_VK_F3 = 0x72,
    LL_VK_F4 = 0x73,
    LL_VK_F5 = 0x74,
    LL_VK_F6 = 0x75,
    LL_VK_F7 = 0x76,
    LL_VK_F8 = 0x77,
    LL_VK_F9 = 0x78,
    LL_VK_F10 = 0x79,
    LL_VK_F11 = 0x7A,
    LL_VK_F12 = 0x7B,
    LL_VK_F13 = 0x7C,
    LL_VK_F14 = 0x7D,
    LL_VK_F15 = 0x7E,
    LL_VK_F16 = 0x7F,
    LL_VK_F17 = 0x80,
    LL_VK_F18 = 0x81,
    LL_VK_F19 = 0x82,
    LL_VK_F20 = 0x83,
    LL_VK_F21 = 0x84,
    LL_VK_F22 = 0x85,
    LL_VK_F23 = 0x86,
    LL_VK_F24 = 0x87,
    LL_VK_NUMLOCK = 0x90,
    LL_VK_SCROLL = 0x91,
    LL_VK_LSHIFT = 0xA0,
    LL_VK_RSHIFT = 0xA1,
    LL_VK_LCONTROL = 0xA2,
    LL_VK_RCONTROL = 0xA3,
    LL_VK_LMENU = 0xA4,
    LL_VK_RMENU = 0xA5,
    LL_VK_BROWSER_BACK = 0xA6,
    LL_VK_BROWSER_FORWARD = 0xA7,
    LL_VK_BROWSER_REFRESH = 0xA8,
    LL_VK_BROWSER_STOP = 0xA9,
    LL_VK_BROWSER_SEARCH = 0xAA,
    LL_VK_BROWSER_FAVORITES = 0xAB,
    LL_VK_BROWSER_HOME = 0xAC,
    LL_VK_VOLUME_MUTE = 0xAD,
    LL_VK_VOLUME_DOWN = 0xAE,
    LL_VK_VOLUME_UP = 0xAF,
    LL_VK_MEDIA_NEXT_TRACK = 0xB0,
    LL_VK_MEDIA_PREV_TRACK = 0xB1,
    LL_VK_MEDIA_STOP = 0xB2,
    LL_VK_MEDIA_PLAY_PAUSE = 0xB3,
    LL_VK_MEDIA_LAUNCH_MAIL = 0xB4,
    LL_VK_MEDIA_LAUNCH_MEDIA_SELECT = 0xB5,
    LL_VK_MEDIA_LAUNCH_APP1 = 0xB6,
    LL_VK_MEDIA_LAUNCH_APP2 = 0xB7,
    LL_VK_OEM_1 = 0xBA,
    LL_VK_OEM_PLUS = 0xBB,
    LL_VK_OEM_COMMA = 0xBC,
    LL_VK_OEM_MINUS = 0xBD,
    LL_VK_OEM_PERIOD = 0xBE,
    LL_VK_OEM_2 = 0xBF,
    LL_VK_OEM_3 = 0xC0,
    LL_VK_OEM_4 = 0xDB,
    LL_VK_OEM_5 = 0xDC,
    LL_VK_OEM_6 = 0xDD,
    LL_VK_OEM_7 = 0xDE,
    LL_VK_OEM_8 = 0xDF,
    LL_VK_OEM_102 = 0xE2,
    LL_VK_PROCESSKEY = 0xE5,
    LL_VK_PACKET = 0xE7,
    LL_VK_ATTN = 0xF6,
    LL_VK_CRSEL = 0xF7,
    LL_VK_EXSEL = 0xF8,
    LL_VK_EREOF = 0xF9,
    LL_VK_PLAY = 0xFA,
    LL_VK_ZOOM = 0xFB,
    LL_VK_NONAME = 0xFC,
    LL_VK_PA1 = 0xFD,
    LL_VK_OEM_CLEAR = 0xFE,
};

U32 LLKeyboardSDL::mapSDLtoWin( U32 aSymbol )
{
    // Map SDLK_ virtual keys to Windows LL_VK_ virtual keys for plugins
    // (notably Dullahan/CEF) that take Win32-style keysyms.
    //
    // Text characters travel via SDL_EVENT_TEXT_INPUT and don't need to be
    // translated through this table — the Windows VK_<letter>/<digit> codes
    // just happen to coincide with their ASCII representation.
    //
    // The lookup is wrapped in a function-local static const map so the
    // construction is thread-safe (C++11 magic statics) and the table is
    // built exactly once.
    static const std::unordered_map<U32, U32> sdl_to_win = []() {
        std::unordered_map<U32, U32> m;

        // Control / whitespace
        m[ SDLK_UNKNOWN    ] = (U32)WindowsVK::LL_VK_UNKNOWN;
        m[ SDLK_BACKSPACE  ] = (U32)WindowsVK::LL_VK_BACK;
        m[ SDLK_TAB        ] = (U32)WindowsVK::LL_VK_TAB;
        m[ SDLK_CLEAR      ] = (U32)WindowsVK::LL_VK_CLEAR;
        m[ SDLK_RETURN     ] = (U32)WindowsVK::LL_VK_RETURN;
        m[ SDLK_PAUSE      ] = (U32)WindowsVK::LL_VK_PAUSE;
        m[ SDLK_ESCAPE     ] = (U32)WindowsVK::LL_VK_ESCAPE;
        m[ SDLK_SPACE      ] = (U32)WindowsVK::LL_VK_SPACE;
        m[ SDLK_DELETE     ] = (U32)WindowsVK::LL_VK_DELETE;

        // OEM punctuation
        m[ SDLK_APOSTROPHE   ] = (U32)WindowsVK::LL_VK_OEM_7;
        m[ SDLK_COMMA        ] = (U32)WindowsVK::LL_VK_OEM_COMMA;
        m[ SDLK_MINUS        ] = (U32)WindowsVK::LL_VK_OEM_MINUS;
        m[ SDLK_PERIOD       ] = (U32)WindowsVK::LL_VK_OEM_PERIOD;
        m[ SDLK_SLASH        ] = (U32)WindowsVK::LL_VK_OEM_2;
        m[ SDLK_SEMICOLON    ] = (U32)WindowsVK::LL_VK_OEM_1;
        m[ SDLK_LESS         ] = (U32)WindowsVK::LL_VK_OEM_102;
        m[ SDLK_EQUALS       ] = (U32)WindowsVK::LL_VK_OEM_PLUS;
        m[ SDLK_LEFTBRACKET  ] = (U32)WindowsVK::LL_VK_OEM_4;
        m[ SDLK_BACKSLASH    ] = (U32)WindowsVK::LL_VK_OEM_5;
        m[ SDLK_RIGHTBRACKET ] = (U32)WindowsVK::LL_VK_OEM_6;
        m[ SDLK_GRAVE        ] = (U32)WindowsVK::LL_VK_OEM_8;

        // Digits
        m[ SDLK_0 ] = (U32)WindowsVK::LL_VK_0;
        m[ SDLK_1 ] = (U32)WindowsVK::LL_VK_1;
        m[ SDLK_2 ] = (U32)WindowsVK::LL_VK_2;
        m[ SDLK_3 ] = (U32)WindowsVK::LL_VK_3;
        m[ SDLK_4 ] = (U32)WindowsVK::LL_VK_4;
        m[ SDLK_5 ] = (U32)WindowsVK::LL_VK_5;
        m[ SDLK_6 ] = (U32)WindowsVK::LL_VK_6;
        m[ SDLK_7 ] = (U32)WindowsVK::LL_VK_7;
        m[ SDLK_8 ] = (U32)WindowsVK::LL_VK_8;
        m[ SDLK_9 ] = (U32)WindowsVK::LL_VK_9;

        // Letters
        m[ SDLK_A ] = (U32)WindowsVK::LL_VK_A;
        m[ SDLK_B ] = (U32)WindowsVK::LL_VK_B;
        m[ SDLK_C ] = (U32)WindowsVK::LL_VK_C;
        m[ SDLK_D ] = (U32)WindowsVK::LL_VK_D;
        m[ SDLK_E ] = (U32)WindowsVK::LL_VK_E;
        m[ SDLK_F ] = (U32)WindowsVK::LL_VK_F;
        m[ SDLK_G ] = (U32)WindowsVK::LL_VK_G;
        m[ SDLK_H ] = (U32)WindowsVK::LL_VK_H;
        m[ SDLK_I ] = (U32)WindowsVK::LL_VK_I;
        m[ SDLK_J ] = (U32)WindowsVK::LL_VK_J;
        m[ SDLK_K ] = (U32)WindowsVK::LL_VK_K;
        m[ SDLK_L ] = (U32)WindowsVK::LL_VK_L;
        m[ SDLK_M ] = (U32)WindowsVK::LL_VK_M;
        m[ SDLK_N ] = (U32)WindowsVK::LL_VK_N;
        m[ SDLK_O ] = (U32)WindowsVK::LL_VK_O;
        m[ SDLK_P ] = (U32)WindowsVK::LL_VK_P;
        m[ SDLK_Q ] = (U32)WindowsVK::LL_VK_Q;
        m[ SDLK_R ] = (U32)WindowsVK::LL_VK_R;
        m[ SDLK_S ] = (U32)WindowsVK::LL_VK_S;
        m[ SDLK_T ] = (U32)WindowsVK::LL_VK_T;
        m[ SDLK_U ] = (U32)WindowsVK::LL_VK_U;
        m[ SDLK_V ] = (U32)WindowsVK::LL_VK_V;
        m[ SDLK_W ] = (U32)WindowsVK::LL_VK_W;
        m[ SDLK_X ] = (U32)WindowsVK::LL_VK_X;
        m[ SDLK_Y ] = (U32)WindowsVK::LL_VK_Y;
        m[ SDLK_Z ] = (U32)WindowsVK::LL_VK_Z;

        // Numpad: use the dedicated VK_DECIMAL/SUBTRACT/ADD/DIVIDE/MULTIPLY
        // codes rather than the OEM operator keys, so plugins can tell a
        // numpad keypress apart from the equivalent keystroke on the row.
        m[ SDLK_KP_DIVIDE   ] = (U32)WindowsVK::LL_VK_DIVIDE;
        m[ SDLK_KP_MULTIPLY ] = (U32)WindowsVK::LL_VK_MULTIPLY;
        m[ SDLK_KP_MINUS    ] = (U32)WindowsVK::LL_VK_SUBTRACT;
        m[ SDLK_KP_PLUS     ] = (U32)WindowsVK::LL_VK_ADD;
        m[ SDLK_KP_PERIOD   ] = (U32)WindowsVK::LL_VK_DECIMAL;
        m[ SDLK_KP_EQUALS   ] = (U32)WindowsVK::LL_VK_OEM_PLUS;
        m[ SDLK_KP_ENTER    ] = (U32)WindowsVK::LL_VK_RETURN;
        // The numlock-off variants of KP digits map to navigation keys
        // (this matches the LL_VK_ values plugins expect on Windows).
        m[ SDLK_KP_0 ] = (U32)WindowsVK::LL_VK_INSERT;
        m[ SDLK_KP_1 ] = (U32)WindowsVK::LL_VK_END;
        m[ SDLK_KP_2 ] = (U32)WindowsVK::LL_VK_DOWN;
        m[ SDLK_KP_3 ] = (U32)WindowsVK::LL_VK_NEXT;
        m[ SDLK_KP_4 ] = (U32)WindowsVK::LL_VK_LEFT;
        m[ SDLK_KP_5 ] = (U32)WindowsVK::LL_VK_NUMPAD5;
        m[ SDLK_KP_6 ] = (U32)WindowsVK::LL_VK_RIGHT;
        m[ SDLK_KP_7 ] = (U32)WindowsVK::LL_VK_HOME;
        m[ SDLK_KP_8 ] = (U32)WindowsVK::LL_VK_UP;
        m[ SDLK_KP_9 ] = (U32)WindowsVK::LL_VK_PRIOR;

        // Lock keys / system keys
        m[ SDLK_NUMLOCKCLEAR ] = (U32)WindowsVK::LL_VK_NUMLOCK;
        m[ SDLK_SCROLLLOCK   ] = (U32)WindowsVK::LL_VK_SCROLL;
        m[ SDLK_CAPSLOCK     ] = (U32)WindowsVK::LL_VK_CAPITAL;
        m[ SDLK_HELP         ] = (U32)WindowsVK::LL_VK_HELP;
        m[ SDLK_PRINTSCREEN  ] = (U32)WindowsVK::LL_VK_SNAPSHOT;
        m[ SDLK_CANCEL       ] = (U32)WindowsVK::LL_VK_CANCEL;
        m[ SDLK_APPLICATION  ] = (U32)WindowsVK::LL_VK_APPS;
        m[ SDLK_MENU         ] = (U32)WindowsVK::LL_VK_MENU;

        // Navigation
        m[ SDLK_UP       ] = (U32)WindowsVK::LL_VK_UP;
        m[ SDLK_DOWN     ] = (U32)WindowsVK::LL_VK_DOWN;
        m[ SDLK_RIGHT    ] = (U32)WindowsVK::LL_VK_RIGHT;
        m[ SDLK_LEFT     ] = (U32)WindowsVK::LL_VK_LEFT;
        m[ SDLK_INSERT   ] = (U32)WindowsVK::LL_VK_INSERT;
        m[ SDLK_HOME     ] = (U32)WindowsVK::LL_VK_HOME;
        m[ SDLK_END      ] = (U32)WindowsVK::LL_VK_END;
        m[ SDLK_PAGEUP   ] = (U32)WindowsVK::LL_VK_PRIOR;
        m[ SDLK_PAGEDOWN ] = (U32)WindowsVK::LL_VK_NEXT;

        // Function keys
        m[ SDLK_F1  ] = (U32)WindowsVK::LL_VK_F1;
        m[ SDLK_F2  ] = (U32)WindowsVK::LL_VK_F2;
        m[ SDLK_F3  ] = (U32)WindowsVK::LL_VK_F3;
        m[ SDLK_F4  ] = (U32)WindowsVK::LL_VK_F4;
        m[ SDLK_F5  ] = (U32)WindowsVK::LL_VK_F5;
        m[ SDLK_F6  ] = (U32)WindowsVK::LL_VK_F6;
        m[ SDLK_F7  ] = (U32)WindowsVK::LL_VK_F7;
        m[ SDLK_F8  ] = (U32)WindowsVK::LL_VK_F8;
        m[ SDLK_F9  ] = (U32)WindowsVK::LL_VK_F9;
        m[ SDLK_F10 ] = (U32)WindowsVK::LL_VK_F10;
        m[ SDLK_F11 ] = (U32)WindowsVK::LL_VK_F11;
        m[ SDLK_F12 ] = (U32)WindowsVK::LL_VK_F12;
        m[ SDLK_F13 ] = (U32)WindowsVK::LL_VK_F13;
        m[ SDLK_F14 ] = (U32)WindowsVK::LL_VK_F14;
        m[ SDLK_F15 ] = (U32)WindowsVK::LL_VK_F15;

        // Modifiers — left and right collapse to a single VK
        m[ SDLK_RSHIFT ] = (U32)WindowsVK::LL_VK_SHIFT;
        m[ SDLK_LSHIFT ] = (U32)WindowsVK::LL_VK_SHIFT;
        m[ SDLK_RCTRL  ] = (U32)WindowsVK::LL_VK_CONTROL;
        m[ SDLK_LCTRL  ] = (U32)WindowsVK::LL_VK_CONTROL;
        m[ SDLK_RALT   ] = (U32)WindowsVK::LL_VK_MENU;
        m[ SDLK_LALT   ] = (U32)WindowsVK::LL_VK_MENU;

        return m;
    }();

    auto it = sdl_to_win.find(aSymbol);
    return (it != sdl_to_win.end()) ? it->second : aSymbol;
}
