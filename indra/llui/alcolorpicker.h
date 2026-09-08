/**
 * @file alcolorpicker.h
 * @brief A colour chosen by eye: a hue ring, a shade square, and the channels.
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

#pragma once

#include "lluictrl.h"
#include "v4color.h"

// A colour chosen the way a colour is chosen: a hue ring with the shades of
// that hue inside it, the three channels of each of the two ways of saying
// a colour, and a strip of the ones that go with what is already picked.
//
// Hue, saturation and value are the state, not the RGB: dragging value to
// nothing and back has to come back to the hue it left, and a colour that
// is only ever kept as three bytes cannot do that.
//
// It holds no floater and no swatch. It is a control with a colour, which
// is what lets it sit inline in a property grid, inside a popover, or in a
// floater of its own without knowing which it is in.
class ALColorPicker : public LLUICtrl
{
public:
    AL_VIEW_TYPE(ALColorPicker, LLUICtrl);

    struct Params : public LLInitParam::Block<Params, LLUICtrl::Params>
    {
        Optional<bool>  show_alpha;
        Params();
    };

    // The colour as four floats, which is what the rest of the UI speaks.
    void setColor(const LLColor4& color);
    const LLColor4& color() const { return mColor; }

    // LLSD is the string "r, g, b, a", so a caller writing a file gets what
    // a file writes and a caller holding a colour gets the colour.
    void setValue(const LLSD& value) override;
    LLSD getValue() const override;

    void draw() override;
    bool handleMouseDown(S32 x, S32 y, MASK mask) override;
    bool handleMouseUp(S32 x, S32 y, MASK mask) override;
    bool handleHover(S32 x, S32 y, MASK mask) override;
    void reshape(S32 width, S32 height, bool called_from_parent = true) override;

protected:
    friend class LLUICtrlFactory;
    ALColorPicker(const Params& p);
    ~ALColorPicker() override;

private:
    // What is being dragged, since a drag belongs to whatever it started on
    // however far outside it the pointer then goes.
    enum class Grab : U8
    {
        None,
        Ring,
        Square,
        Hue, Saturation, Value,
        Red, Green, Blue, Alpha
    };

    void layout();
    void fromHSV();
    void toHSV();

    void drawRing() const;
    void drawSquare() const;
    void drawHarmonies() const;
    void drawChannels() const;
    void drawSlider(const LLRect& track, F32 fraction, const LLColor4& from, const LLColor4& to,
                    bool hue_track, const std::string& label) const;

    Grab grabAt(S32 x, S32 y) const;
    void apply(Grab grab, S32 x, S32 y);
    LLColor4 harmony(S32 index) const;

    LLColor4    mColor;
    F32         mHue = 0.f;
    F32         mSat = 0.f;
    F32         mVal = 0.f;
    bool        mShowAlpha = true;
    Grab        mGrab = Grab::None;

    // Where each part sits, worked out from the rect on every reshape.
    LLRect      mRingBox;       // the square the ring is inscribed in
    LLRect      mSquare;        // the shades of the hue
    LLRect      mHarmonies;
    LLRect      mChannels;
};
