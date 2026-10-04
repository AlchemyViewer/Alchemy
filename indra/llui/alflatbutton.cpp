/**
 * @file alflatbutton.cpp
 * @brief A glyph that is a button: flat, as an editor's bars have them.
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

#include "alflatbutton.h"

#include "alsaid.h"

#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llrender2dutils.h"
#include "llwindow.h"

ALFlatButton::ALFlatButton(const Params& p, std::string glyph, bool toggle, const LLUIColor& ink, const LLUIColor& lit)
:   LLUICtrl(p),
    mGlyph(std::move(glyph)),
    mToggle(toggle),
    mInk(ink),
    mLit(lit)
{
    // Made here rather than by the factory, which is what would have
    // read the parameter: a stop for Tab, as a button is.
    setTabStop(true);
}

std::string ALFlatButton::getToolTip() const
{
    const std::string tip = LLUICtrl::getToolTip();
    return mKey == KEY_NONE || tip.empty()
               ? tip
               : alSaid("TipWithKeys", "[TIP] ([KEYS])", { { "[TIP]", tip }, { "[KEYS]", LLKeyboard::stringFromAccelerator(mKeyMask, mKey) } });
}

void ALFlatButton::draw()
{
    const F32    alpha = getDrawContext().mAlpha;
    const LLRect local = getLocalRect();
    if (mOn)
    {
        gl_rect_2d(local, mLit.get() % alpha);
    }
    else if (mHover && getEnabled())
    {
        gl_rect_2d(local, mInk.get() % (0.12f * alpha));
    }
    LLColor4 ink = mInk.get() % alpha;
    if (!getEnabled())
    {
        ink.mV[VALPHA] *= 0.35f;
    }
    const LLFontGL* font = LLFontGL::getFontSansSerifSmall();
    font->renderUTF8(mGlyph, 0, local.getCenterX(), local.getCenterY() - font->getLineHeight() / 2 + 1, ink, LLFontGL::HCENTER, LLFontGL::BOTTOM);
    if (hasFocus())
    {
        // Where the keyboard is, once Tab has brought it here.
        gl_rect_2d(local, gFocusMgr.getFocusColor() % alpha, false);
    }
    mHover = false;
}

bool ALFlatButton::handleHover(S32 x, S32 y, MASK mask)
{
    mHover = true;
    // A button: the arrow, not the text's cursor under the bar.
    if (LLWindow* window = getWindow())
    {
        window->setCursor(UI_CURSOR_ARROW);
    }
    return true;
}

bool ALFlatButton::handleMouseDown(S32 x, S32 y, MASK mask)
{
    press();
    return true;
}

bool ALFlatButton::handleUnicodeCharHere(llwchar uni_char)
{
    // Pressed from the keyboard as a button is: Space, which comes as the
    // character, once however long it is held; and Return.
    if (uni_char == ' ')
    {
        if (!gKeyboard || !gKeyboard->getKeyRepeated(' '))
        {
            press();
        }
        return true;
    }
    return LLUICtrl::handleUnicodeCharHere(uni_char);
}

bool ALFlatButton::handleKeyHere(KEY key, MASK mask)
{
    if (key == KEY_RETURN && mask == MASK_NONE)
    {
        press();
        return true;
    }
    return LLUICtrl::handleKeyHere(key, mask);
}

void ALFlatButton::press()
{
    if (!getEnabled())
    {
        return;
    }
    if (mToggle)
    {
        mOn = !mOn;
    }
    onCommit();
}
