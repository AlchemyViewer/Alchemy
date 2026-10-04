/**
 * @file alflatbutton.h
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

#ifndef AL_ALFLATBUTTON_H
#define AL_ALFLATBUTTON_H

#include "alviewtype.h"
#include "llkeyboard.h"
#include "lluicolor.h"
#include "lluictrl.h"

#include <string>

// A glyph that is a button: flat, as the modern editors' bars have them
// (the find bar's, a comparison's), lit when it is a toggle that is on,
// shaded under the mouse, dimmed when it cannot be pressed. A stop for Tab
// as a button is, pressed by Space or Return from the keyboard as well as
// by the mouse; pressing it commits it, a toggle turned first.
//
// Its colours are the bar's, which follows the view it is over: set with
// setInk and setLit whenever the theme moves.
class ALFlatButton : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALFlatButton, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
    };

    ALFlatButton(const Params& p, std::string glyph, bool toggle, const LLUIColor& ink, const LLUIColor& lit);

    void               setGlyph(const std::string& glyph) { mGlyph = glyph; }
    const std::string& glyph() const { return mGlyph; }
    // The key that presses it, said after its tip; whoever has the keyboard
    // is the one to take it.
    void setKey(KEY key, MASK mask)
    {
        mKey     = key;
        mKeyMask = mask;
    }

    // Put together as it is shown rather than when it is made: a key's
    // name is the viewer's to give, in the viewer's language.
    std::string getToolTip() const override;
    void        setInk(const LLColor4& ink) { mInk = ink; }
    void        setLit(const LLColor4& lit) { mLit = lit; }
    bool        getToggleState() const { return mOn; }
    void        setToggleState(bool on) { mOn = on; }

    void draw() override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override { return true; }
    bool handleUnicodeCharHere(llwchar uni_char) override;
    bool handleKeyHere(KEY key, MASK mask) override;

    // As a press: a toggle turned, and committed; nothing where it cannot
    // be pressed.
    void press();

private:
    std::string mGlyph;
    KEY         mKey     = KEY_NONE;
    MASK        mKeyMask = MASK_NONE;
    bool        mToggle  = false;
    bool        mOn      = false;
    bool        mHover   = false;
    LLUIColor   mInk;
    LLUIColor   mLit;
};

#endif // AL_ALFLATBUTTON_H
